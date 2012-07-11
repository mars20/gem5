/*
 * Copyright (c) 2007 The Hewlett-Packard Development Company
 * All rights reserved.
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
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
 * Authors: Gabe Black
 */

#include <cmath>
#include "arch/x86/isa_traits.hh"
#include "arch/x86/pagetable.hh"
#include "sim/serialize.hh"

namespace X86ISA
{

TlbEntry::TlbEntry(Addr asn, Addr _vaddr, Addr _paddr) :
    paddr(_paddr), vaddr(_vaddr), logBytes(PageShift), writable(true),
    user(true), uncacheable(false), global(false), patBit(0), noExec(false)
{}

void
TlbEntry::serialize(std::ostream &os)
{
    SERIALIZE_SCALAR(paddr);
    SERIALIZE_SCALAR(vaddr);
    SERIALIZE_SCALAR(logBytes);
    SERIALIZE_SCALAR(writable);
    SERIALIZE_SCALAR(user);
    SERIALIZE_SCALAR(uncacheable);
    SERIALIZE_SCALAR(global);
    SERIALIZE_SCALAR(patBit);
    SERIALIZE_SCALAR(noExec);
    SERIALIZE_SCALAR(lruSeq);
}

void
TlbEntry::unserialize(Checkpoint *cp, const std::string &section)
{
    UNSERIALIZE_SCALAR(paddr);
    UNSERIALIZE_SCALAR(vaddr);
    //
    // The logBytes scalar variable replaced the previous size variable.
    // The following code maintains backwards compatibility with previous
    // checkpoints using the old size variable.
    //
    if (UNSERIALIZE_OPT_SCALAR(logBytes) == false) {
        int size;
        UNSERIALIZE_SCALAR(size);
        logBytes = log2(size);
    }
    UNSERIALIZE_SCALAR(writable);
    UNSERIALIZE_SCALAR(user);
    UNSERIALIZE_SCALAR(uncacheable);
    UNSERIALIZE_SCALAR(global);
    UNSERIALIZE_SCALAR(patBit);
    UNSERIALIZE_SCALAR(noExec);
    if (UNSERIALIZE_OPT_SCALAR(lruSeq) == false) {
        lruSeq = 0;
    }
}

}
