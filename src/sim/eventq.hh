/*
 * Copyright (c) 2000-2005 The Regents of The University of Michigan
 * Copyright (c) 2013 Advanced Micro Devices, Inc.
 * Copyright (c) 2013 Mark D. Hill and David A. Wood
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Steve Reinhardt
 *          Nathan Binkert
 */

/* @file
 * EventQueue interfaces
 */

#ifndef __SIM_EVENTQ_HH__
#define __SIM_EVENTQ_HH__

#include <algorithm>
#include <cassert>
#include <climits>
#include <iosfwd>
#include <mutex>
#include <string>

#include "base/flags.hh"
#include "base/misc.hh"
#include "base/types.hh"
#include "debug/Event.hh"
#include "sim/serialize.hh"

class EventQueue;       // forward declaration
class BaseGlobalEvent;

//! Simulation Quantum for multiple eventq simulation.
//! The quantum value is the period length after which the queues
//! synchronize themselves with each other. This means that any
//! event to scheduled on Queue A which is generated by an event on
//! Queue B should be at least simQuantum ticks away in future.
extern Tick simQuantum;

//! Current number of allocated main event queues.
extern uint32_t numMainEventQueues;

//! Array for main event queues.
extern std::vector<EventQueue *> mainEventQueue;

#ifndef SWIG
//! The current event queue for the running thread. Access to this queue
//! does not require any locking from the thread.

extern __thread EventQueue *_curEventQueue;

#endif

//! Current mode of execution: parallel / serial
extern bool inParallelMode;

//! Function for returning eventq queue for the provided
//! index. The function allocates a new queue in case one
//! does not exist for the index, provided that the index
//! is with in bounds.
EventQueue *getEventQueue(uint32_t index);

inline EventQueue *curEventQueue() { return _curEventQueue; }
inline void curEventQueue(EventQueue *q) { _curEventQueue = q; }

/**
 * Common base class for Event and GlobalEvent, so they can share flag
 * and priority definitions and accessor functions.  This class should
 * not be used directly.
 */
class EventBase
{
  protected:   
    typedef unsigned short FlagsType;
    typedef ::Flags<FlagsType> Flags;

    static const FlagsType PublicRead    = 0x003f; // public readable flags
    static const FlagsType PublicWrite   = 0x001d; // public writable flags
    static const FlagsType Squashed      = 0x0001; // has been squashed
    static const FlagsType Scheduled     = 0x0002; // has been scheduled
    static const FlagsType AutoDelete    = 0x0004; // delete after dispatch
    static const FlagsType AutoSerialize = 0x0008; // must be serialized
    static const FlagsType IsExitEvent   = 0x0010; // special exit event
    static const FlagsType IsMainQueue   = 0x0020; // on main event queue
    static const FlagsType Initialized   = 0x7a40; // somewhat random bits
    static const FlagsType InitMask      = 0xffc0; // mask for init bits

  public:
    typedef int8_t Priority;

    /// Event priorities, to provide tie-breakers for events scheduled
    /// at the same cycle.  Most events are scheduled at the default
    /// priority; these values are used to control events that need to
    /// be ordered within a cycle.

    /// Minimum priority
    static const Priority Minimum_Pri =          SCHAR_MIN;

    /// If we enable tracing on a particular cycle, do that as the
    /// very first thing so we don't miss any of the events on
    /// that cycle (even if we enter the debugger).
    static const Priority Debug_Enable_Pri =          -101;

    /// Breakpoints should happen before anything else (except
    /// enabling trace output), so we don't miss any action when
    /// debugging.
    static const Priority Debug_Break_Pri =           -100;

    /// CPU switches schedule the new CPU's tick event for the
    /// same cycle (after unscheduling the old CPU's tick event).
    /// The switch needs to come before any tick events to make
    /// sure we don't tick both CPUs in the same cycle.
    static const Priority CPU_Switch_Pri =             -31;

    /// For some reason "delayed" inter-cluster writebacks are
    /// scheduled before regular writebacks (which have default
    /// priority).  Steve?
    static const Priority Delayed_Writeback_Pri =       -1;

    /// Default is zero for historical reasons.
    static const Priority Default_Pri =                  0;

    /// Serailization needs to occur before tick events also, so
    /// that a serialize/unserialize is identical to an on-line
    /// CPU switch.
    static const Priority Serialize_Pri =               32;

    /// CPU ticks must come after other associated CPU events
    /// (such as writebacks).
    static const Priority CPU_Tick_Pri =                50;

    /// Statistics events (dump, reset, etc.) come after
    /// everything else, but before exit.
    static const Priority Stat_Event_Pri =              90;

    /// Progress events come at the end.
    static const Priority Progress_Event_Pri =          95;

    /// If we want to exit on this cycle, it's the very last thing
    /// we do.
    static const Priority Sim_Exit_Pri =               100;

    /// Maximum priority
    static const Priority Maximum_Pri =          SCHAR_MAX;
};

/*
 * An item on an event queue.  The action caused by a given
 * event is specified by deriving a subclass and overriding the
 * process() member function.
 *
 * Caution, the order of members is chosen to maximize data packing.
 */
class Event : public EventBase, public Serializable
{
    friend class EventQueue;

  private:
    // The event queue is now a linked list of linked lists.  The
    // 'nextBin' pointer is to find the bin, where a bin is defined as
    // when+priority.  All events in the same bin will be stored in a
    // second linked list (a stack) maintained by the 'nextInBin'
    // pointer.  The list will be accessed in LIFO order.  The end
    // result is that the insert/removal in 'nextBin' is
    // linear/constant, and the lookup/removal in 'nextInBin' is
    // constant/constant.  Hopefully this is a significant improvement
    // over the current fully linear insertion.
    Event *nextBin;
    Event *nextInBin;

    static Event *insertBefore(Event *event, Event *curr);
    static Event *removeItem(Event *event, Event *last);

    Tick _when;         //!< timestamp when event should be processed
    Priority _priority; //!< event priority
    Flags flags;

#ifndef NDEBUG
    /// Global counter to generate unique IDs for Event instances
    static Counter instanceCounter;

    /// This event's unique ID.  We can also use pointer values for
    /// this but they're not consistent across runs making debugging
    /// more difficult.  Thus we use a global counter value when
    /// debugging.
    Counter instance;

    /// queue to which this event belongs (though it may or may not be
    /// scheduled on this queue yet)
    EventQueue *queue;
#endif

#ifdef EVENTQ_DEBUG
    Tick whenCreated;   //!< time created
    Tick whenScheduled; //!< time scheduled
#endif

    void
    setWhen(Tick when, EventQueue *q)
    {
        _when = when;
#ifndef NDEBUG
        queue = q;
#endif
#ifdef EVENTQ_DEBUG
        whenScheduled = ::curTick();
#endif
    }

    bool
    initialized() const
    {
        return this && (flags & InitMask) == Initialized;
    }

  protected:
    /// Accessor for flags.
    Flags
    getFlags() const
    {
        return flags & PublicRead;
    }

    bool
    isFlagSet(Flags _flags) const
    {
        assert(_flags.noneSet(~PublicRead));
        return flags.isSet(_flags);
    }

    /// Accessor for flags.
    void
    setFlags(Flags _flags)
    {
        assert(_flags.noneSet(~PublicWrite));
        flags.set(_flags);
    }

    void
    clearFlags(Flags _flags)
    {
        assert(_flags.noneSet(~PublicWrite));
        flags.clear(_flags);
    }

    void
    clearFlags()
    {
        flags.clear(PublicWrite);
    }

    // This function isn't really useful if TRACING_ON is not defined
    virtual void trace(const char *action);     //!< trace event activity

  public:

    /*
     * Event constructor
     * @param queue that the event gets scheduled on
     */
    Event(Priority p = Default_Pri, Flags f = 0)
        : nextBin(NULL), nextInBin(NULL), _priority(p),
          flags(Initialized | f)
    {
        assert(f.noneSet(~PublicWrite));
#ifndef NDEBUG
        instance = ++instanceCounter;
        queue = NULL;
#endif
#ifdef EVENTQ_DEBUG
        whenCreated = ::curTick();
        whenScheduled = 0;
#endif
    }

    virtual ~Event();
    virtual const std::string name() const;

    /// Return a C string describing the event.  This string should
    /// *not* be dynamically allocated; just a const char array
    /// describing the event class.
    virtual const char *description() const;

    /// Dump the current event data
    void dump() const;

  public:
    /*
     * This member function is invoked when the event is processed
     * (occurs).  There is no default implementation; each subclass
     * must provide its own implementation.  The event is not
     * automatically deleted after it is processed (to allow for
     * statically allocated event objects).
     *
     * If the AutoDestroy flag is set, the object is deleted once it
     * is processed.
     */
    virtual void process() = 0;

    /// Determine if the current event is scheduled
    bool scheduled() const { return flags.isSet(Scheduled); }

    /// Squash the current event
    void squash() { flags.set(Squashed); }

    /// Check whether the event is squashed
    bool squashed() const { return flags.isSet(Squashed); }

    /// See if this is a SimExitEvent (without resorting to RTTI)
    bool isExitEvent() const { return flags.isSet(IsExitEvent); }

    /// Get the time that the event is scheduled
    Tick when() const { return _when; }

    /// Get the event priority
    Priority priority() const { return _priority; }

    //! If this is part of a GlobalEvent, return the pointer to the
    //! Global Event.  By default, there is no GlobalEvent, so return
    //! NULL.  (Overridden in GlobalEvent::BarrierEvent.)
    virtual BaseGlobalEvent *globalEvent() { return NULL; }

#ifndef SWIG
    virtual void serialize(std::ostream &os);
    virtual void unserialize(Checkpoint *cp, const std::string &section);

    //! This function is required to support restoring from checkpoints
    //! when running with multiple queues. Since we still have not thrashed
    //! out all the details on checkpointing, this function is most likely
    //! to be revisited in future.
    virtual void unserialize(Checkpoint *cp, const std::string &section,
                     EventQueue *eventq);
#endif
};

#ifndef SWIG
inline bool
operator<(const Event &l, const Event &r)
{
    return l.when() < r.when() ||
        (l.when() == r.when() && l.priority() < r.priority());
}

inline bool
operator>(const Event &l, const Event &r)
{
    return l.when() > r.when() ||
        (l.when() == r.when() && l.priority() > r.priority());
}

inline bool
operator<=(const Event &l, const Event &r)
{
    return l.when() < r.when() ||
        (l.when() == r.when() && l.priority() <= r.priority());
}
inline bool
operator>=(const Event &l, const Event &r)
{
    return l.when() > r.when() ||
        (l.when() == r.when() && l.priority() >= r.priority());
}

inline bool
operator==(const Event &l, const Event &r)
{
    return l.when() == r.when() && l.priority() == r.priority();
}

inline bool
operator!=(const Event &l, const Event &r)
{
    return l.when() != r.when() || l.priority() != r.priority();
}
#endif

/*
 * Queue of events sorted in time order
 */
class EventQueue : public Serializable
{
  private:
    std::string objName;
    Event *head;
    Tick _curTick;

    //! Mutex to protect async queue.
    std::mutex *async_queue_mutex;

    //! List of events added by other threads to this event queue.
    std::list<Event*> async_queue;

    std::recursive_mutex service_mutex;

    //! Insert / remove event from the queue. Should only be called
    //! by thread operating this queue.
    void insert(Event *event);
    void remove(Event *event);

    //! Function for adding events to the async queue. The added events
    //! are added to main event queue later. Threads, other than the
    //! owning thread, should call this function instead of insert().
    void asyncInsert(Event *event);

    EventQueue(const EventQueue &);

  public:
    EventQueue(const std::string &n);

    virtual const std::string name() const { return objName; }
    void name(const std::string &st) { objName = st; }

    //! Schedule the given event on this queue. Safe to call from any
    //! thread.
    void schedule(Event *event, Tick when, bool global = false);

    //! Deschedule the specified event. Should be called only from the
    //! owning thread.
    void deschedule(Event *event);

    //! Reschedule the specified event. Should be called only from
    //! the owning thread.
    void reschedule(Event *event, Tick when, bool always = false);

    Tick nextTick() const { return head->when(); }
    void setCurTick(Tick newVal) { _curTick = newVal; }
    Tick getCurTick() const { return _curTick; }

    Event *serviceOne();

    // process all events up to the given timestamp.  we inline a
    // quick test to see if there are any events to process; if so,
    // call the internal out-of-line version to process them all.
    void
    serviceEvents(Tick when)
    {
        while (!empty()) {
            if (nextTick() > when)
                break;

            /**
             * @todo this assert is a good bug catcher.  I need to
             * make it true again.
             */
            //assert(head->when() >= when && "event scheduled in the past");
            serviceOne();
        }

        setCurTick(when);
    }

    // return true if no events are queued
    bool empty() const { return head == NULL; }

    void dump() const;

    bool debugVerify() const;

    //! Function for moving events from the async_queue to the main queue.
    void handleAsyncInsertions();

    /**
     *  function for replacing the head of the event queue, so that a
     *  different set of events can run without disturbing events that have
     *  already been scheduled. Already scheduled events can be processed
     *  by replacing the original head back.
     *  USING THIS FUNCTION CAN BE DANGEROUS TO THE HEALTH OF THE SIMULATOR.
     *  NOT RECOMMENDED FOR USE.
     */
    Event* replaceHead(Event* s);

    void setThread(pthread_t thread);
    void kick();

    /**@{*/
    /**
     * Lock an event queue to prevent events from being serviced.
     *
     * If SimObjects need to be called across event queues (e.g.,
     * device accesses), the queue running the device needs to be
     * locked to prevent races between events serviced in the event
     * queue and the events scheduled by the call. This can be useful
     * to enable device accesses when timing is not crucial (e.g.,
     * when fast forwarding using KVM).
     *
     * @warn Scheduling events across event queues can result in
     * non-deterministic simulation results.
     */
    void lock() {
        service_mutex.lock();
    }
    /**
     * Unlock a locked event queue.
     *
     * @see lock()
     */
    void unlock() {
        // SimObjects might have inserted new events into the queue
        // while it was locked. If this was done from a different
        // thread than the thread owning the queue, they will be
        // inserted as asynchronous events. We need to schedule them
        // like normal events since they should be allowed to execute
        // before the global barrier.
        handleAsyncInsertions();
        service_mutex.unlock();
    }
    /**@}*/

#ifndef SWIG
    virtual void serialize(std::ostream &os);
    virtual void unserialize(Checkpoint *cp, const std::string &section);
#endif
};

void dumpMainQueue();

#ifndef SWIG
class EventManager
{
  protected:
    /** A pointer to this object's event queue */
    EventQueue *eventq;

  public:
    EventManager(EventManager &em) : eventq(em.eventq) {}
    EventManager(EventManager *em) : eventq(em->eventq) {}
    EventManager(EventQueue *eq) : eventq(eq) {}

    EventQueue *
    eventQueue() const
    {
        return eventq;
    }

    void
    schedule(Event &event, Tick when)
    {
        eventq->schedule(&event, when);
    }

    void
    scheduleRelative(Event &event, Tick delay)
    {
        eventq->schedule(&event, curTick() + delay);
    }

    void
    deschedule(Event &event)
    {
        eventq->deschedule(&event);
    }

    void
    reschedule(Event &event, Tick when, bool always = false)
    {
        eventq->reschedule(&event, when, always);
    }

    void
    schedule(Event *event, Tick when)
    {
        eventq->schedule(event, when);
    }

    void
    scheduleRelative(Event *event, Tick delay)
    {
        eventq->schedule(event, curTick() + delay);
    }

    void
    deschedule(Event *event)
    {
        eventq->deschedule(event);
    }

    void
    reschedule(Event *event, Tick when, bool always = false)
    {
        eventq->reschedule(event, when, always);
    }

    void setCurTick(Tick newVal) { eventq->setCurTick(newVal); }

    Tick curTick() const { return eventq->getCurTick(); }
};

template <class T, void (T::* F)()>
void
DelayFunction(EventQueue *eventq, Tick when, T *object)
{
    class DelayEvent : public Event
    {
      private:
        T *object;

      public:
        DelayEvent(T *o)
            : Event(Default_Pri, AutoDelete), object(o)
        { }
        void process() { (object->*F)(); }
        const char *description() const { return "delay"; }
    };

    eventq->schedule(new DelayEvent(object), when);
}

template <class T, void (T::* F)()>
class EventWrapper : public Event
{
  private:
    T *object;

  public:
    EventWrapper(T *obj, bool del = false, Priority p = Default_Pri)
        : Event(p), object(obj)
    {
        if (del)
            setFlags(AutoDelete);
    }

    EventWrapper(T &obj, bool del = false, Priority p = Default_Pri)
        : Event(p), object(&obj)
    {
        if (del)
            setFlags(AutoDelete);
    }

    void process() { (object->*F)(); }

    const std::string
    name() const
    {
        return object->name() + ".wrapped_event";
    }

    const char *description() const { return "EventWrapped"; }
};
#endif

#endif // __SIM_EVENTQ_HH__
