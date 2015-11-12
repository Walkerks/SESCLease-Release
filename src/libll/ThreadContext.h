#ifndef THREADCONTEXT_H
#define THREADCONTEXT_H

#include <stdint.h>
#include <cstring>
#include <vector>
#include <set>
#include "Snippets.h"
#include "libemul/AddressSpace.h"
#include "libemul/SignalHandling.h"
#include "libemul/FileSys.h"
#include "libemul/InstDesc.h"
#include "libemul/LinuxSys.h"

#if (defined TM)
#include "libTM/TMContext.h"
#endif

// Contains information for instructions that are at function boundaries about
// the function itself. Entry point instructions (calls) contain arguments and
// the return address, exit point instructions (returns) contain the return value.
struct FuncBoundaryData {
    static FuncBoundaryData createCall(enum FuncName name,
            uint32_t retA, uint32_t a0, uint32_t a1) {
        return FuncBoundaryData(name, true, retA, 0, a0, a1);
    }
    static FuncBoundaryData createRet(enum FuncName name,
            uint32_t retV) {
        return FuncBoundaryData(name, false, 0, retV, 0, 0);
    }
    FuncBoundaryData(enum FuncName name, bool call,
            uint32_t retA, uint32_t retV, uint32_t a0, uint32_t a1):
        funcName(name), isCall(call), ra(retA), rv(retV), arg0(a0), arg1(a1) {}

    enum FuncName funcName;
    bool isCall;        // Call if true, return if false
    uint32_t ra;
    uint32_t rv;
    uint32_t arg0;
    uint32_t arg1;
};

// Struct that contains overall statistics about subsections within an atomic region.
struct TimeTrackerStats {
    TimeTrackerStats(): totalLengths(0), totalCommitted(0), totalAborted(0),
        totalLockWait(0), totalBackoffWait(0), totalMutexWait(0), totalMutex(0)
    {}
    uint64_t totalAccounted() const;
    void print() const;
    void sum(const TimeTrackerStats& other);

    uint64_t totalLengths;
    uint64_t totalCommitted;
    uint64_t totalAborted;
    uint64_t totalLockWait;
    uint64_t totalBackoffWait;
    uint64_t totalMutexWait;
    uint64_t totalMutex;
};

// Enum of various atomic region events
enum AREventType {
    AR_EVENT_INVALID            = 0, // Invalid, uninitialized event
    AR_EVENT_HTM_BEGIN          = 1, // HTM begin event
    AR_EVENT_HTM_ABORT          = 2, // HTM abort event
    AR_EVENT_HTM_COMMIT         = 3, // HTM commit event

    AR_EVENT_LOCK_REQUEST       = 10, // Lock request event (calling lock)
    AR_EVENT_LOCK_ACQUIRE       = 11, // Lock acquire event (returning from lock)
    AR_EVENT_LOCK_RELEASE       = 12, // Lock release event (calling  unlock)

    AR_EVENT_LOCK_WAIT_BEGIN    = 90, // Wait for active lock (calling wait(0))
    AR_EVENT_BACKOFF_BEGIN      = 91, // Random backoff       (calling wait(1))
    AR_EVENT_WAIT_END           = 92, // Wait end             (returning from wait)

    AR_EVENT_NUM_TYPES
};

// Pair of values that indicate an event within an atomic region
class AtomicRegionEvents {
public:
    AtomicRegionEvents(enum AREventType t, Time_t at): type(t), timestamp(at) {}
    enum AREventType getType() const { return type; }
    Time_t getTimestamp() const { return timestamp; }
private:
    enum AREventType type;
    Time_t timestamp;
};

// Used to track timing statistics of atomic regions (between tm_begin and tm_end).
// All timing is done at retire-time of DInsts.
struct AtomicRegionStats {
    AtomicRegionStats() { clear(); }
    void clear();
    void init(Pid_t p, VAddr pc, Time_t at);
    void markEnd(Time_t at) {
        endAt = at;
    }
    void printEvents(const AtomicRegionEvents& current) const;
    void markRetireFuncBoundary(DInst* dinst, const FuncBoundaryData& funcData);
    void markRetireTM(DInst* dinst);
    void calculate(TimeTrackerStats* p_stats);
    void newAREvent(enum AREventType type);

    Pid_t               pid;
    VAddr               startPC;
    Time_t              startAt;
    Time_t              endAt;
    std::vector<AtomicRegionEvents> events;
};

// Holds state that is the result of a single instruction result. This data
// generated at emul stage, and is later passed to the corresponding DInst.
class InstContext {
public:
    InstContext() { clear(); }
    void clear();

    // Whether the memory operation hit in the emul'd private cache
    bool wasHit;
    bool setConflict;
    // Cycles for stalling retire of a tm instruction
    uint32_t    tmLat;
    // User-passed HTM command arg
    uint32_t    tmArg;
    // If this instruction is a function boundary, this contains info about that function
    std::vector<FuncBoundaryData> funcData;

    TMBeginSubtype tmBeginSubtype;
    TMCommitSubtype tmCommitSubtype;
};

// Use this define to debug the simulated application
// It enables call stack tracking
//#define DEBUG_BENCH

class ThreadContext : public GCObject {
public:
    typedef SmartPtr<ThreadContext> pointer;
    static bool ff;
    static bool simDone;
	static int64_t finalSkip;
    static Time_t resetTS;

    AtomicRegionStats       currentRegion;
    static TimeTrackerStats timeTrackerStats;
    TimeTrackerStats        myTimeStats;
private:
    void initialize(bool child);
	void cleanup();
    typedef std::vector<pointer> ContextVector;
    // Static variables
    static ContextVector pid2context;


	// TM
#if (defined TM)
    // Debug flag for making sure we have consistent view of SW tid and HW tid
    uint32_t tmlibUserTid;
#define INVALID_USER_TID (0xDEADDEAD)
    // Unique transaction identifier
    uint64_t tmUtid;
    // Saved thread context
    TMContext *tmContext;
    // Depth of nested transactions
    size_t      tmDepth;
    // User-passed HTM abort argument (valid from abort-begin)
    uint32_t    tmAbortArg;
    // Where user had called HTM "instructions"
    VAddr tmCallsite;
    // Common set of fallback mutex addresses to check if the abort is caused by a fallback
    static std::set<uint32_t> tmFallbackMutexCAddrs;
#endif

    // Memory Mapping

    // Lower and upper bound for stack addresses in this thread
    VAddr myStackAddrLb;
    VAddr myStackAddrUb;

    // Local Variables
private:
    int32_t pid;		// process id

    // Execution mode of this thread
    ExecMode execMode;
    // Register file(s)
    RegVal regs[NumOfRegs];
    // Address space for this thread
    AddressSpace::pointer addressSpace;
    // Instruction pointer
    VAddr     iAddr;
    // Instruction descriptor
    InstDesc *iDesc;
    // Virtual address generated by the last memory access instruction
    VAddr     dAddr;
    InstContext instContext;
    size_t    nDInsts;
    // Number of retired DInsts during this thread's lifetime
    size_t    nRetiredInsts;
    // Number of executed DInsts during this thread's lifetime
    size_t    nExedInsts;

    // HACK to balance calls/returns
    typedef void (*retHandler_t)(InstDesc *, ThreadContext *);
    std::vector<std::pair<VAddr, retHandler_t> > retHandlers;
    std::vector<std::pair<VAddr, retHandler_t> > retHandlersSaved;

public:
    void markRetire(DInst* dinst);

    // Function call/return hook handling
    void createCall(enum FuncName funcName, uint32_t retA, uint32_t arg0, uint32_t arg1) {
        instContext.funcData.push_back(FuncBoundaryData::createCall(funcName, retA, arg0, arg1));
    }
    void createRet(enum FuncName funcName, uint32_t retV) {
        instContext.funcData.push_back(FuncBoundaryData::createRet(funcName, retV));
    }
    void saveCallRetStack() {
        retHandlersSaved = retHandlers;
    }
    void restoreCallRetStack() {
        retHandlers = retHandlersSaved;
    }
    void addCall(VAddr ra, retHandler_t handler) {
        retHandlers.push_back(std::make_pair(ra, handler));
    }
    void handleReturns(VAddr destIAddr, InstDesc *inst) {
        while(!retHandlers.empty()) {
            std::pair<VAddr, retHandler_t> handler_ra = retHandlers.back();
            if(handler_ra.first == destIAddr) {
                handler_ra.second(inst, this);
                retHandlers.pop_back();
            } else {
                break;
            }
        }
    }
    bool retsEmpty() const { return retHandlers.empty(); }

#if (defined TM)
    // Transactional Helper Methods
    size_t getTMdepth()     const { return tmDepth; }
    bool isInTM()           const { return getTMdepth() > 0; }
    TMState_e getTMState()  const { return tmCohManager ? tmCohManager->getState(pid) : TM_INVALID; }
    uint32_t getTMAbortArg() const { return tmAbortArg; }

    TMContext* getTMContext() const { return tmContext; }
    void setTMContext(TMContext* newTMContext) { tmContext = newTMContext; }

    void setTMCallsite(VAddr ra) { tmCallsite = ra; }
    VAddr getTMCallsite() const { return tmCallsite; }

    // Transactional Methods
    void setTMlibUserTid(uint32_t arg);
    TMBCStatus beginTransaction(InstDesc* inst);
    TMBCStatus commitTransaction(InstDesc* inst);
    TMBCStatus abortTransaction(InstDesc* inst, TMAbortType_e abortType);
    TMBCStatus abortTransaction(InstDesc* inst);

    TMBCStatus userBeginTM(InstDesc* inst, uint32_t arg) {
        instContext.tmArg = arg;
        TMBCStatus status = beginTransaction(inst);
        return status;
    }
    TMBCStatus userCommitTM(InstDesc* inst, uint32_t arg) {
        instContext.tmArg = arg;
        TMBCStatus status = commitTransaction(inst);
        return status;
    }
    void userAbortTM(InstDesc* inst, uint32_t arg);
    void syscallAbortTM(InstDesc* inst);

    void completeAbort(InstDesc* inst);
    uint32_t getBeginRV(TMBCStatus status);
    uint32_t getAbortRV();
    void beginFallback(uint32_t pFallbackMutex);
    void completeFallback();

    // memop NACK handling methods
    void startRetryTimer() {
        startStalling(tmCohManager->getNackRetryStallCycles());
    }
    static bool isFallbackMutexAddr(VAddr cAddr) {
        return tmFallbackMutexCAddrs.find(cAddr) != tmFallbackMutexCAddrs.end();
    }
#endif
    // Thread stalling methods
private:
    Time_t  stallUntil;
public:
    void startStalling(TimeDelta_t amount) {
        if(amount > 0) {
            stallUntil = globalClock + amount;
        }
    }
    bool checkStall() const {
        return stallUntil != 0 && stallUntil >= globalClock;
    }

    static inline int32_t getPidUb(void) {
        return pid2context.size();
    }
    void setMode(ExecMode mode);
    inline ExecMode getMode(void) const {
        return execMode;
    }

    inline const void *getReg(RegName name) const {
        return &(regs[name]);
    }
    inline void *getReg(RegName name) {
        return &(regs[name]);
    }
    void clearRegs(void) {
        memset(regs,0,sizeof(regs));
    }

#if (defined TM)
	void setReg(RegName name, RegVal val) {
		regs[name] = val;
	}
#endif
    // Returns the pid of the context
    Pid_t getPid(void) const {
        return pid;
    }

    void copy(const ThreadContext *src);

    static ThreadContext *getContext(Pid_t pid);

    static ThreadContext *getMainThreadContext(void) {
        return &(*(pid2context[0]));
    }
    static void printPCs(void);

    // BEGIN Memory Mapping
    bool isValidDataVAddr(VAddr vaddr) const {
        return canRead(vaddr,1)||canWrite(vaddr,1);
    }

    ThreadContext(FileSys::FileSys *fileSys);
    ThreadContext(ThreadContext &parent, bool cloneParent,
                  bool cloneFileSys, bool newNameSpace,
                  bool cloneFiles, bool cloneSighand,
                  bool cloneVm, bool cloneThread,
                  SignalID sig, VAddr clearChildTid);
    ~ThreadContext();

    ThreadContext *createChild(bool shareAddrSpace, bool shareSigTable, bool shareOpenFiles, SignalID sig);
    void setAddressSpace(AddressSpace *newAddressSpace);
    AddressSpace *getAddressSpace(void) const {
        I(addressSpace);
        return addressSpace;
    }
    inline void setStack(VAddr stackLb, VAddr stackUb) {
        myStackAddrLb=stackLb;
        myStackAddrUb=stackUb;
    }
    inline VAddr getStackAddr(void) const {
        return myStackAddrLb;
    }
    inline VAddr getStackSize(void) const {
        return myStackAddrUb-myStackAddrLb;
    }

    inline InstDesc *virt2inst(VAddr vaddr) {
        InstDesc *inst=addressSpace->virtToInst(vaddr);
        if(!inst) {
            addressSpace->createTrace(this,vaddr);
            inst=addressSpace->virtToInst(vaddr);
        }
        return inst;
    }

    bool isLocalStackData(VAddr addr) const {
        return (addr>=myStackAddrLb)&&(addr<myStackAddrUb);
    }

    VAddr getStackTop() const {
        return myStackAddrLb;
    }
    // END Memory Mapping

    inline InstDesc *getIDesc(void) const {
        return iDesc;
    }
    inline void updIDesc(ssize_t ddiff) {
        I((ddiff>=-1)&&(ddiff<4));
        iDesc+=ddiff;
    }
    inline VAddr getIAddr(void) const {
        return iAddr;
    }
    inline void setIAddr(VAddr addr) {
        iAddr=addr;
        iDesc=iAddr?virt2inst(addr):0;
    }
    inline void updIAddr(ssize_t adiff, ssize_t ddiff) {
        I((ddiff>=-1)&&(ddiff<4));
        I((adiff>=-4)&&(adiff<=8));
        iAddr+=adiff;
        iDesc+=ddiff;
    }
    inline VAddr getDAddr(void) const {
        return dAddr;
    }
    inline void setDAddr(VAddr addr) {
        dAddr=addr;
    }
    const InstContext& getInstContext() const {
        return instContext;
    }
    InstContext& getInstContext() {
        return instContext;
    }
    void clearInstContext() {
        instContext.clear();
    }
    inline void addDInst(void) {
        nDInsts++;
    }
    inline void delDInst(void) {
        nDInsts--;
    }
    inline size_t getNDInsts(void) {
        return nDInsts;
    }
    inline size_t getNRetiredInsts(void) {
        return nRetiredInsts;
    }
    inline void incNExedInsts(void) {
        nExedInsts++;
    }
    inline size_t getNExedInsts(void) {
        return nExedInsts;
    }
    static inline int32_t nextReady(int32_t startPid) {
        int32_t foundPid=startPid;
        do {
            if(foundPid==(int)(pid2context.size()))
                foundPid=0;
            ThreadContext *context=pid2context[foundPid];
            if(context&&(!context->isSuspended())&&(!context->isExited()))
                return foundPid;
            foundPid++;
        } while(foundPid!=startPid);
        return -1;
    }
    inline bool skipInst(void);
    static int64_t skipInsts(int64_t skipCount);
#if (defined HAS_MEM_STATE)
    inline const MemState &getState(VAddr addr) const {
        return addressSpace->getState(addr);
    }
    inline MemState &getState(VAddr addr) {
        return addressSpace->getState(addr);
    }
#endif
    inline bool canRead(VAddr addr, size_t len) const {
        return addressSpace->canRead(addr,len);
    }
    inline bool canWrite(VAddr addr, size_t len) const {
        return addressSpace->canWrite(addr,len);
    }
    void    writeMemFromBuf(VAddr addr, size_t len, const void *buf);
//  ssize_t writeMemFromFile(VAddr addr, size_t len, int32_t fd, bool natFile, bool usePread=false, off_t offs=0);
    void    writeMemWithByte(VAddr addr, size_t len, uint8_t c);
    void    readMemToBuf(VAddr addr, size_t len, void *buf);
//  ssize_t readMemToFile(VAddr addr, size_t len, int32_t fd, bool natFile);
    ssize_t readMemString(VAddr stringVAddr, size_t maxSize, char *dstStr);
    template<class T>
    inline void readMemTM(VAddr addr, T oval, T* p_val) {
        if(tmContext == NULL) {
            fail("tmContext is NULL");
        }
        tmContext->cacheAccess<T>(addr, oval, p_val);
    }
    template<class T>
    inline T readMemRaw(VAddr addr) {
        if(sizeof(T)>sizeof(MemAlignType)) {
            fail("ThreadContext:writeMemRaw with a too-large type\n");
//      T tmp;
//      I(canRead(addr,sizeof(T)));
//      readMemToBuf(addr,sizeof(T),&tmp);
//      return tmp;
        }
//    for(size_t i=0;i<(sizeof(T)+MemState::Granularity-1)/MemState::Granularity;i++)
//      if(getState(addr+i*MemState::Granularity).st==0)
//        fail("Uninitialized read found\n");


        if(addressSpace->canRead(addr) == false) {
            fail("%d reading from non-readable page\n", pid);
        }
        return addressSpace->read<T>(addr);
    }
    template<class T>
    inline void writeMemTM(VAddr addr, const T &val) {
        if(tmContext == NULL) {
            fail("tmContext is NULL");
        }
        tmContext->cacheWrite<T>(addr, val);
    }
    template<class T>
    inline void writeMemRaw(VAddr addr, const T &val) {
        //   if((addr>=0x4d565c)&&(addr<0x4d565c+12)){
        //     I(0);
        //     I(iAddr!=0x004bb428);
        //     I(iAddr!=0x004c8604);
        //     const char *fname="Unknown";
        //     if(iAddr)
        //       fname=getAddressSpace()->getFuncName(getAddressSpace()->getFuncAddr(iAddr));
        //     printf("Write 0x%08x to 0x%08x at 0x%08x in %s\n",
        //       val,addr,iAddr,fname);
        //   }
        if(sizeof(T)>sizeof(MemAlignType)) {
            fail("ThreadContext:writeMemRaw with a too-large type\n");
//      if(!canWrite(addr,sizeof(val)))
//	return false;
//      writeMemFromBuf(addr,sizeof(val),&val);
//      return true;
        }
//    for(size_t i=0;i<(sizeof(T)+MemState::Granularity-1)/MemState::Granularity;i++)
//      getState(addr+i*MemState::Granularity).st=1;

        if(addressSpace->canWrite(addr) == false) {
            fail("%d writing to non-writeable page\n", pid);
        }
        addressSpace->write<T>(addr,val);
    }
#if (defined DEBUG_BENCH)
    VAddr readMemWord(VAddr addr);
#endif

    //
    // File system
    //
private:
    FileSys::FileSys::pointer fileSys;
    FileSys::OpenFiles::pointer openFiles;
public:
    FileSys::FileSys *getFileSys(void) const {
        return fileSys;
    }
    FileSys::OpenFiles *getOpenFiles(void) const {
        return openFiles;
    }

    //
    // Signal handling
    //
private:
    SignalTable::pointer sigTable;
    SignalSet   sigMask;
    SignalQueue maskedSig;
    SignalQueue readySig;
    bool        suspSig;
public:
    void setSignalTable(SignalTable *newSigTable) {
        sigTable=newSigTable;
    }
    SignalTable *getSignalTable(void) const {
        return sigTable;
    }
    void suspend(void);
    void signal(SigInfo *sigInfo);
    void resume(void);
    const SignalSet &getSignalMask(void) const {
        return sigMask;
    }
    void setSignalMask(const SignalSet &newMask) {
        sigMask=newMask;
        for(size_t i=0; i<maskedSig.size(); i++) {
            SignalID sig=maskedSig[i]->signo;
            if(!sigMask.test(sig)) {
                readySig.push_back(maskedSig[i]);
                maskedSig[i]=maskedSig.back();
                maskedSig.pop_back();
            }
        }
        for(size_t i=0; i<readySig.size(); i++) {
            SignalID sig=readySig[i]->signo;
            if(sigMask.test(sig)) {
                maskedSig.push_back(readySig[i]);
                readySig[i]=readySig.back();
                readySig.pop_back();
            }
        }
        if((!readySig.empty())&&suspSig)
            resume();
    }
    bool hasReadySignal(void) const {
        return !readySig.empty();
    }
    SigInfo *nextReadySignal(void) {
        I(hasReadySignal());
        SigInfo *sigInfo=readySig.back();
        readySig.pop_back();
        return sigInfo;
    }

    // System state

    LinuxSys *mySystem;
    LinuxSys *getSystem(void) const {
        return mySystem;
    }

    // Parent/Child relationships
private:
    typedef std::set<int> IntSet;
    // Thread id of this thread
    int32_t tid;
    // tid of the thread group leader
    int32_t tgid;
    // This set is empty for threads that are not thread group leader
    // In a thread group leader, this set contains the other members of the thread group
    IntSet tgtids;

    // Process group Id is the PId of the process group leader
    int32_t pgid;

    int parentID;
    IntSet childIDs;
    // Signal sent to parent when this thread dies/exits
    SignalID  exitSig;
    // Futex to clear when this thread dies/exits
    VAddr clear_child_tid;
    // Robust list head pointer
    VAddr robust_list;
public:
    int32_t gettgid(void) const {
        return tgid;
    }
    size_t gettgtids(int tids[], size_t slots) const {
        IntSet::const_iterator it=tgtids.begin();
        for(size_t i=0; i<slots; i++,it++)
            tids[i]=*it;
        return tgtids.size();
    }
    int32_t gettid(void) const {
        return tid;
    }
    int32_t getpgid(void) const {
        return pgid;
    }
    int getppid(void) const {
        return parentID;
    }
    void setRobustList(VAddr headptr) {
        robust_list=headptr;
    }
    void setTidAddress(VAddr tidptr) {
        clear_child_tid=tidptr;
    }
    int32_t  getParentID(void) const {
        return parentID;
    }
    bool hasChildren(void) const {
        return !childIDs.empty();
    }
    bool isChildID(int32_t id) const {
        return (childIDs.find(id)!=childIDs.end());
    }
    int32_t findZombieChild(void) const;
    SignalID getExitSig(void) {
        return exitSig;
    }
private:
    bool     exited;
    int32_t      exitCode;
    SignalID killSignal;
public:
    bool isSuspended(void) const {
        return suspSig;
    }
    bool isExited(void) const {
        return exited;
    }
    int32_t getExitCode(void) const {
        return exitCode;
    }
    bool isKilled(void) const {
        return (killSignal!=SigNone);
    }
    SignalID getKillSignal(void) const {
        return killSignal;
    }
    // Exit this process
    // Returns: true if exit complete, false if process is now zombie
    bool exit(int32_t code);
    // Reap an exited process
    void reap();
    void doKill(SignalID sig) {
        I(!isExited());
        I(!isKilled());
        I(sig!=SigNone);
        killSignal=sig;
    }

    // Debugging

    class CallStackEntry {
    public:
        VAddr entry;
        VAddr ra;
        VAddr sp;
        bool  tailr;
        CallStackEntry(VAddr entry, VAddr  ra, VAddr sp, bool tailr)
            : entry(entry), ra(ra), sp(sp), tailr(tailr) {
        }
    };
    typedef std::vector<CallStackEntry> CallStack;
    CallStack callStack;

    void execCall(VAddr entry, VAddr  ra, VAddr sp);
    void execRet(VAddr entry, VAddr ra, VAddr sp);
    void dumpCallStack(void);
    void clearCallStack(void);

public:
    // Event tracing
    bool spinning;
    static bool inMain;
    static size_t numThreads;

    std::ofstream tracefile;
    static std::ofstream& getTracefile() {
        return getMainThreadContext()->tracefile;
    }
};

#endif // THREADCONTEXT_H
