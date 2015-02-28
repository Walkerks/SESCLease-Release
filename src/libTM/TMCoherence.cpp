#include <cmath>
#include <algorithm>
#include <iostream>
#include "libsuc/nanassert.h"
#include "SescConf.h"
#include "libll/Instruction.h"
#include "TMCoherence.h"

using namespace std;

TMCoherence *tmCohManager = 0;
uint64_t TMCoherence::nextUtid = 0;
/////////////////////////////////////////////////////////////////////////////////////////
// Factory function for all TM Coherence objects. Use different concrete classes
// depending on SescConf
/////////////////////////////////////////////////////////////////////////////////////////
TMCoherence *TMCoherence::create(int32_t nProcs) {
    TMCoherence* newCohManager;

    string method = SescConf->getCharPtr("TransactionalMemory","method");
    int cacheLineSize = SescConf->getInt("TransactionalMemory","cacheLineSize");
    int numLines = SescConf->getInt("TransactionalMemory","numLines");
	int returnArgType = SescConf->getInt("TransactionalMemory","returnArgType");
    if(method == "EE") {
        newCohManager = new TMEECoherence(nProcs, cacheLineSize, numLines, returnArgType);
    } else if(method == "LL") {
        newCohManager = new TMLLCoherence(nProcs, cacheLineSize, numLines, returnArgType);
    } else if(method == "LE") {
        newCohManager = new TMLECoherence(nProcs, cacheLineSize, numLines, returnArgType);
    } else {
        MSG("unknown TM method, using LE");
        newCohManager = new TMLECoherence(nProcs, cacheLineSize, numLines, returnArgType);
    }

    return newCohManager;
}

/////////////////////////////////////////////////////////////////////////////////////////
// Abstract super-class of all TM policies. Contains the external interface and common
// implementations
/////////////////////////////////////////////////////////////////////////////////////////
TMCoherence::TMCoherence(const char tmStyle[], int32_t procs, int lineSize, int lines, int argType):
        nProcs(procs), cacheLineSize(lineSize), numLines(lines), returnArgType(argType),
        nackStallBaseCycles(1), nackStallCap(1),
        numCommits("tm:numCommits"),
        numAborts("tm:numAborts"),
        abortTypes("tm:abortTypes"),
        tmLoads("tm:loads"),
        tmStores("tm:stores"),
        tmLoadMisses("tm:loadMisses"),
        tmStoreMisses("tm:storeMisses"),
        numAbortsCausedBeforeAbort("tm:numAbortsCausedBeforeAbort"),
        numAbortsCausedBeforeCommit("tm:numAbortsCausedBeforeCommit"),
        linesReadHist("tm:linesReadHist"),
        linesWrittenHist("tm:linesWrittenHist") {

    MSG("Using %s TM", tmStyle);

    caches = new PrivateCache("privatel1", nProcs);

    for(Pid_t pid = 0; pid < nProcs; ++pid) {
        transStates.push_back(TransState(pid));
        // Initialize maps to enable at() use
        linesRead[pid].clear();
        linesWritten[pid].clear();
    }
}

bool TMCoherence::hadWrote(VAddr caddr, Pid_t pid) {
    return linesWritten[pid].find(caddr) != linesWritten[pid].end();
}

bool TMCoherence::hadRead(VAddr caddr, Pid_t pid) {
    return linesRead[pid].find(caddr) != linesRead[pid].end();
}
void TMCoherence::getWritersExcept(VAddr caddr, Pid_t pid, std::set<Pid_t>& w) {
    std::map<VAddr, std::list<Pid_t> >::iterator i_line;
    i_line = writers.find(caddr);
    if(i_line != writers.end()) {
        w.insert(i_line->second.begin(), i_line->second.end());
        w.erase(pid);
    }
}
void TMCoherence::getReadersExcept(VAddr caddr, Pid_t pid, std::set<Pid_t>& r) {
    std::map<VAddr, std::list<Pid_t> >::iterator i_line;
    i_line = readers.find(caddr);
    if(i_line != readers.end()) {
        r.insert(i_line->second.begin(), i_line->second.end());
        r.erase(pid);
    }
}
void TMCoherence::removeFromList(std::list<Pid_t>& list, Pid_t pid) {
    std::list<Pid_t>::iterator i_list = list.begin();
    while(i_list != list.end()) {
        if(*i_list == pid) {
            list.erase(i_list++);
        } else {
            ++i_list;
        }
    }
}

void TMCoherence::removeTransaction(Pid_t pid) {
    std::map<VAddr, std::list<Pid_t> >::iterator i_line;
    std::set<VAddr>::iterator i_wroteTo;
    for(i_wroteTo = linesWritten[pid].begin(); i_wroteTo != linesWritten[pid].end(); ++i_wroteTo) {
        i_line = writers.find(*i_wroteTo);
        if(i_line == writers.end()) {
            fail("linesWritten and writers mismatch\n");
        }
        removeFromList(i_line->second, pid);
        if(i_line->second.empty()) {
            writers.erase(i_line);
        }
        if(std::find(i_line->second.begin(), i_line->second.end(), pid) != i_line->second.end()) {
            fail("Remove fail?");
        }
    }
    std::set<VAddr>::iterator i_readFrom;
    for(i_readFrom = linesRead[pid].begin(); i_readFrom != linesRead[pid].end(); ++i_readFrom) {
        i_line = readers.find(*i_readFrom);
        if(i_line == readers.end()) {
            fail("linesRead and readers mismatch\n");
        }
        removeFromList(i_line->second, pid);
        if(i_line->second.empty()) {
            readers.erase(i_line);
        }
        if(std::find(i_line->second.begin(), i_line->second.end(), pid) != i_line->second.end()) {
            fail("Remove fail?");
        }
    }
    linesRead[pid].clear();
    linesWritten[pid].clear();
}

void TMCoherence::beginTrans(Pid_t pid, InstDesc* inst) {
	if(!transStates[pid].getRestartPending()) {
        // This is a new transaction instance
    } // Else a restarted transaction

    // Reset Statistics
    numAbortsCaused[pid] = 0;

    // Do the begin
    removeTransaction(pid);
	transStates[pid].begin(TMCoherence::nextUtid++);
}

void TMCoherence::commitTrans(Pid_t pid) {
    // Update Statistics
    numCommits.inc();
    numAbortsCausedBeforeCommit.add(numAbortsCaused[pid]);
    linesReadHist.sample(getNumReads(pid));
    linesWrittenHist.sample(getNumWrites(pid));

    // Do the commit
    removeTransaction(pid);
    caches->clearTransactional(pid);
    transStates[pid].commit();
}
void TMCoherence::abortTrans(Pid_t pid) {
	transStates[pid].startAborting();
}
void TMCoherence::completeAbortTrans(Pid_t pid) {
    // Update Statistics
    numAborts.inc();
    numAbortsCausedBeforeAbort.add(numAbortsCaused[pid]);
    abortTypes.sample(transStates[pid].getAbortType());
    linesReadHist.sample(getNumReads(pid));
    linesWrittenHist.sample(getNumWrites(pid));

    // Do the completeAbort
    removeTransaction(pid);
    caches->clearTransactional(pid);
    transStates[pid].completeAbort();
}

void TMCoherence::markTransAborted(Pid_t victimPid, Pid_t aborterPid, VAddr caddr, TMAbortType_e abortType) {
    uint64_t aborterUtid = transStates[aborterPid].getUtid();

    if(transStates[victimPid].getState() != TM_ABORTING) {
        transStates[victimPid].markAbort(aborterPid, aborterUtid, caddr, abortType);
        removeTransaction(victimPid);
        if(victimPid != aborterPid && transStates[aborterPid].getState() == TM_RUNNING) {
            numAbortsCaused[aborterPid]++;
        }
    } // Else victim is already aborting, so leave it alone
}

void TMCoherence::markTransAborted(std::set<Pid_t>& aborted, Pid_t aborterPid, VAddr caddr, TMAbortType_e abortType) {
	set<Pid_t>::iterator i_aborted;
    for(i_aborted = aborted.begin(); i_aborted != aborted.end(); ++i_aborted) {
		if(*i_aborted == aborterPid) {
            fail("Aborter is also the aborted?");
        }
        markTransAborted(*i_aborted, aborterPid, caddr, abortType);
	}
}
void TMCoherence::readTrans(Pid_t pid, int tid, VAddr raddr, VAddr caddr) {
    I(transStates[pid].getState() == TM_RUNNING);

    if(!hadRead(caddr, pid)) {
        linesRead[pid].insert(caddr);
        readers[caddr].push_back(pid);
    } else {
        if(find(readers[caddr].begin(), readers[caddr].end(), pid)
                == readers[caddr].end()) {
            fail("readers and linesRead mistmatch in add\n");
        }
    }
}
void TMCoherence::writeTrans(Pid_t pid, int tid, VAddr raddr, VAddr caddr) {
    I(transStates[pid].getState() == TM_RUNNING);

    if(!hadWrote(caddr, pid)) {
        linesWritten[pid].insert(caddr);
        writers[caddr].push_back(pid);
    } else {
        if(find(writers[caddr].begin(), writers[caddr].end(), pid)
                == writers[caddr].end()) {
            fail("writers and linesWritten mistmatch in add\n");
        }
    }
}
void TMCoherence::nackTrans(Pid_t pid) {
    transStates[pid].startNacking();
}

///
// Entry point for TM begin operation. Check for nesting and then call the real begin.
TMBCStatus TMCoherence::begin(Pid_t pid, InstDesc* inst) {
    if(transStates[pid].getDepth() > 0) {
        fail("Nested transactions not tested\n");
		transStates[pid].beginNested();
		return TMBC_IGNORE;
	} else {
		return myBegin(pid, inst);
	}
}

///
// Entry point for TM begin operation. Check for nesting and then call the real begin.
TMBCStatus TMCoherence::commit(Pid_t pid, int tid) {
	if(transStates[pid].getState() == TM_MARKABORT) {
		return TMBC_ABORT;
	} else if(transStates[pid].getDepth() > 1) {
		transStates[pid].commitNested();
		return TMBC_IGNORE;
	} else {
		return myCommit(pid, tid);
	}
}

///
// Entry point for TM abort operation. If the abort type is driven externally (syscall/user),
// then mark the transaction as aborted, else 
TMBCStatus TMCoherence::abort(Pid_t pid, int tid, TMAbortType_e abortType) {
    if(abortType == TM_ATYPE_SYSCALL || abortType == TM_ATYPE_USER) {
        transStates[pid].markAbort(pid, transStates[pid].getUtid(), 0, abortType);
    } else if(abortType != 0) {
        // Abort type internal, so should not be set
        fail("Unknown abort type");
    }
    return myAbort(pid, tid);
}

///
// Entry point for TM complete abort operation (to be called after an aborted TM returns to
// tm.begin).
TMBCStatus TMCoherence::completeAbort(Pid_t pid) {
    if(transStates[pid].getState() == TM_ABORTING) {
        myCompleteAbort(pid);
    }
    return TMBC_SUCCESS;
}

///
// Function that tells the TM engine that a fallback path for this transaction has been used,
// so reset any statistics. Used for statistics that run across multiple retires.
void TMCoherence::completeFallback(Pid_t pid) {
    transStates[pid].completeFallback();
    removeTransaction(pid);
}

///
// Helper function that looks at all private caches and invalidates every active transaction.
void TMCoherence::invalidateSharers(InstDesc* inst, ThreadContext* context, VAddr raddr) {
    Pid_t pid   = context->getPid();
	VAddr caddr = addrToCacheLine(raddr);

    for(Pid_t p = 0; p < (Pid_t)nProcs; ++p) {
        if(p != pid) {
            PrivateCache::Line* line = caches->findLine(p, raddr);
            if(line) {
                if(line->isTransactional()) {
                    markTransAborted(p, pid, caddr, TM_ATYPE_EVICTION);
                }
                line->invalidate();
            }
        }
    } // End foreach(pid)
}

///
// Entry point for TM read operation. Checks transaction state and then calls the real read.
TMRWStatus TMCoherence::read(InstDesc* inst, ThreadContext* context, VAddr raddr, MemOpStatus* p_opStatus) {
    Pid_t pid   = context->getPid();
	VAddr caddr = addrToCacheLine(raddr);
	if(transStates[pid].getState() == TM_MARKABORT) {
		return TMRW_ABORT;
	} else {
        caches->doLoad(inst, context, raddr, p_opStatus);
        tmLoads.inc();

        if(p_opStatus->setConflict) {
            markTransAborted(pid, pid, caddr, TM_ATYPE_SETCONFLICT);
            return TMRW_ABORT;
        }

        return myRead(pid, 0, raddr);
    }
}

///
// Entry point for TM write operation. Checks transaction state and then calls the real write.
TMRWStatus TMCoherence::write(InstDesc* inst, ThreadContext* context, VAddr raddr, MemOpStatus* p_opStatus) {
    Pid_t pid   = context->getPid();
	VAddr caddr = addrToCacheLine(raddr);
	if(transStates[pid].getState() == TM_MARKABORT) {
		return TMRW_ABORT;
	} else {
        invalidateSharers(inst, context, raddr);

        caches->doStore(inst, context, raddr, p_opStatus);
        tmStores.inc();

        if(p_opStatus->setConflict) {
            markTransAborted(pid, pid, caddr, TM_ATYPE_SETCONFLICT);
            return TMRW_ABORT;
        }

        return myWrite(pid, 0, raddr);
    }
}

///
// Entry point for a non-transactional read, i.e. when a thread not inside a transaction.
// If this read conflicts with data read as part of another transaction, abort the transaction.
TMRWStatus TMCoherence::nonTMread(InstDesc* inst, ThreadContext* context, VAddr raddr, MemOpStatus* p_opStatus) {
    Pid_t pid   = context->getPid();
	VAddr caddr = addrToCacheLine(raddr);

    I(!hadRead(caddr, pid));
    I(!hadWrote(caddr, pid));

    caches->doLoad(inst, context, raddr, p_opStatus);

    // Abort writers once we try to read
    set<Pid_t> aborted;
    getWritersExcept(caddr, pid, aborted);

    markTransAborted(aborted, pid, caddr, TM_ATYPE_NONTM);

    return TMRW_SUCCESS;
}

///
// Entry point for a non-transactional write, i.e. when a thread not inside a transaction.
// If this write conflicts with data read as part of another transaction, abort the transaction.
TMRWStatus TMCoherence::nonTMwrite(InstDesc* inst, ThreadContext* context, VAddr raddr, MemOpStatus* p_opStatus) {
    Pid_t pid   = context->getPid();
	VAddr caddr = addrToCacheLine(raddr);

    I(!hadRead(caddr, pid));
    I(!hadWrote(caddr, pid));

    invalidateSharers(inst, context, raddr);

    caches->doStore(inst, context, raddr, p_opStatus);


    // Abort everyone once we try to write
    set<Pid_t> aborted;
    getReadersExcept(caddr, pid, aborted);
    getWritersExcept(caddr, pid, aborted);

    markTransAborted(aborted, pid, caddr, TM_ATYPE_NONTM);

    return TMRW_SUCCESS;
}

///
// A basic type of TM begin that will be used if child does not override
TMBCStatus TMCoherence::myBegin(Pid_t pid, InstDesc* inst) {
    beginTrans(pid, inst);
    return TMBC_SUCCESS;
}

///
// A basic type of TM abort if child does not override
TMBCStatus TMCoherence::myAbort(Pid_t pid, int tid) {
	abortTrans(pid);
	return TMBC_SUCCESS;
}

///
// A basic type of TM commit if child does not override
TMBCStatus TMCoherence::myCommit(Pid_t pid, int tid) {
    commitTrans(pid);
    return TMBC_SUCCESS;
}

///
// A basic type of TM complete abort if child does not override
void TMCoherence::myCompleteAbort(Pid_t pid) {
    completeAbortTrans(pid);
}

/////////////////////////////////////////////////////////////////////////////////////////
// Eager-eager coherence. TMs update shared memory as soon as they write, and only
// on conflict do they try to roll back any updates that the transaction had made.
// Follows LogTM TM policy.
/////////////////////////////////////////////////////////////////////////////////////////
TMEECoherence::TMEECoherence(int32_t nProcs, int lineSize, int lines, int argType):
        TMCoherence("Eager/Eager", nProcs, lineSize, lines, argType), cycleFlags(nProcs) {
}
TMRWStatus TMEECoherence::myRead(Pid_t pid, int tid, VAddr raddr) {
	VAddr caddr = addrToCacheLine(raddr);

	// If we had been NACKed, we can now be released
	if(transStates[pid].getState() == TM_NACKED) {
		transStates[pid].resumeAfterNack();
	}

    if(writers[caddr].size() >= 1 && !hadWrote(caddr, pid)) {
        list<Pid_t>::iterator i_writer = writers[caddr].begin();
        Pid_t aborterPid = *i_writer;

        if(aborterPid == pid) {
            // Grab the first reader than isn't us
            ++i_writer;
            aborterPid = *i_writer;
        }

        Time_t nackTimestamp = transStates[aborterPid].getTimestamp();
        Time_t myTimestamp = transStates[pid].getTimestamp();

        if(nackTimestamp <= myTimestamp && cycleFlags[pid]) {
            markTransAborted(pid, aborterPid, caddr, TM_ATYPE_DEFAULT);
            return TMRW_ABORT;
        } else {
            if(nackTimestamp >= myTimestamp) {
                cycleFlags[aborterPid] = true;
            }

            nackTrans(pid);
            return TMRW_NACKED;
        }
    } else {
        readTrans(pid, tid, raddr, caddr);
        return TMRW_SUCCESS;
    }
}

TMRWStatus TMEECoherence::myWrite(Pid_t pid, int tid, VAddr raddr) {
	VAddr caddr = addrToCacheLine(raddr);

	// If we had been NACKed, we can now be released
	if(transStates[pid].getState() == TM_NACKED) {
		transStates[pid].resumeAfterNack();
	}

    if(readers[caddr].size() > 1 || ((readers[caddr].size() == 1) && !hadRead(caddr, pid))) {
        // If there is more than one reader, or there is a single reader who happens not to be us
        list<Pid_t>::iterator i_reader = readers[caddr].begin();
        Pid_t aborterPid = *i_reader;

        if(aborterPid == pid) {
            // Grab the first reader than isn't us
            ++i_reader;
            aborterPid = *i_reader;
        }

        Time_t nackTimestamp = transStates[aborterPid].getTimestamp();
        Time_t myTimestamp = transStates[pid].getTimestamp();

        if(nackTimestamp <= myTimestamp && cycleFlags[pid]) {
            markTransAborted(pid, aborterPid, caddr, TM_ATYPE_DEFAULT);
            return TMRW_ABORT;
        } else {
            if(nackTimestamp >= myTimestamp) {
                cycleFlags[aborterPid] = true;
            }

            nackTrans(pid);
            return TMRW_NACKED;
        }
    } else if(writers[caddr].size() > 1 || ((writers[caddr].size() == 1) && !hadWrote(caddr, pid))) {
        list<Pid_t>::iterator i_writer = writers[caddr].begin();
        Pid_t aborterPid = *i_writer;

        if(aborterPid == pid) {
            // Grab the first reader than isn't us
            ++i_writer;
            aborterPid = *i_writer;
        }

        Time_t nackTimestamp = transStates[aborterPid].getTimestamp();
        Time_t myTimestamp = transStates[pid].getTimestamp();

        if(nackTimestamp <= myTimestamp && cycleFlags[pid]) {
            markTransAborted(pid, aborterPid, caddr, TM_ATYPE_DEFAULT);
            return TMRW_ABORT;
        } else {
            if(nackTimestamp >= myTimestamp) {
                cycleFlags[aborterPid] = true;
            }

            nackTrans(pid);
            return TMRW_NACKED;
        }
    } else {
        writeTrans(pid, tid, raddr, caddr);
        return TMRW_SUCCESS;
    }
}

TMBCStatus TMEECoherence::myBegin(Pid_t pid, InstDesc* inst) {
    cycleFlags[pid] = false;
    beginTrans(pid, inst);
    return TMBC_SUCCESS;
}

TMBCStatus TMEECoherence::myAbort(Pid_t pid, int tid) {
	cycleFlags[pid] = false;
	abortTrans(pid);

	return TMBC_SUCCESS;
}

TMBCStatus TMEECoherence::myCommit(Pid_t pid, int tid) {
    commitTrans(pid);
    cycleFlags[pid] = false;

    return TMBC_SUCCESS;
}

/////////////////////////////////////////////////////////////////////////////////////////
// Lazy-lazy coherence. TMs are allowed to run until commit, which then they are checked
// for any memory conflicts. Follows Josep's group's TM policy
/////////////////////////////////////////////////////////////////////////////////////////
TMLLCoherence::TMLLCoherence(int32_t nProcs, int lineSize, int lines, int argType):
        TMCoherence("Lazy/Lazy", nProcs, lineSize, lines, argType) {

	currentCommitter = INVALID_PID; 
}
TMRWStatus TMLLCoherence::myRead(Pid_t pid, int tid, VAddr raddr) {
	VAddr caddr = addrToCacheLine(raddr);

    readTrans(pid, tid, raddr, caddr);
    return TMRW_SUCCESS;
}

TMRWStatus TMLLCoherence::myWrite(Pid_t pid, int tid, VAddr raddr) {
	VAddr caddr = addrToCacheLine(raddr);

    writeTrans(pid, tid, raddr, caddr);
    return TMRW_SUCCESS;
}

TMBCStatus TMLLCoherence::myCommit(Pid_t pid, int tid) {
    if(currentCommitter == INVALID_PID) {
        // Stop other transactions from being able to commit
        currentCommitter = pid;

        // "Lazily" check the read and write sets and abort anyone who conflicts with me
        uint64_t utid = transStates[pid].getUtid();
        set<VAddr>::iterator i_line;
        set<Pid_t> aborted;
        for(i_line = linesWritten[pid].begin(); i_line != linesWritten[pid].end(); ++i_line) {
            VAddr caddr = *i_line;

            aborted.clear();
            getReadersExcept(caddr, pid, aborted);
            getWritersExcept(caddr, pid, aborted);
            markTransAborted(aborted, pid, caddr, TM_ATYPE_DEFAULT);
        }

        // Now do the "commit"
        commitTrans(pid);

        // Allow other transaction to commit again
        currentCommitter = INVALID_PID;
        return TMBC_SUCCESS;
    } else {
        nackTrans(pid);

        return TMBC_NACK;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////
// Lazy-eager coherence. This is the most simple style of TM, and used in TSX
/////////////////////////////////////////////////////////////////////////////////////////
TMLECoherence::TMLECoherence(int32_t nProcs, int lineSize, int lines, int argType):
        TMCoherence("Lazy/Eager", nProcs, lineSize, lines, argType) {
}
TMRWStatus TMLECoherence::myRead(Pid_t pid, int tid, VAddr raddr) {
	VAddr caddr = addrToCacheLine(raddr);
    uint64_t utid = transStates[pid].getUtid();

    // Abort writers once we try to read
    set<Pid_t> aborted;
    getWritersExcept(caddr, pid, aborted);
    markTransAborted(aborted, pid, caddr, TM_ATYPE_DEFAULT);

    readTrans(pid, tid, raddr, caddr);
    return TMRW_SUCCESS;
}

TMRWStatus TMLECoherence::myWrite(Pid_t pid, int tid, VAddr raddr) {
	VAddr caddr = addrToCacheLine(raddr);
    uint64_t utid = transStates[pid].getUtid();

    // Abort everyone once we try to write
    set<Pid_t> aborted;
    getReadersExcept(caddr, pid, aborted);
    getWritersExcept(caddr, pid, aborted);
    markTransAborted(aborted, pid, caddr, TM_ATYPE_DEFAULT);

    writeTrans(pid, tid, raddr, caddr);
    return TMRW_SUCCESS;
}

