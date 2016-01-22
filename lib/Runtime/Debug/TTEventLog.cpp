//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#include "RuntimeDebugPch.h"

#if ENABLE_TTD

namespace TTD
{
    TTDExceptionFramePopper::TTDExceptionFramePopper()
        : m_log(nullptr)
    {
        ;
    }

    TTDExceptionFramePopper::~TTDExceptionFramePopper()
    {
#if ENABLE_TTD_DEBUGGING
        //we didn't clear this so an exception was thrown and we are propagating
        if(this->m_log != nullptr)
        {
            //if it doesn't have an exception frame then this is the frame where the exception was thrown so record our info
            this->m_log->PopCallEventException(!this->m_log->HasImmediateExceptionFrame());
        }
#endif
    }

    void TTDExceptionFramePopper::PushInfo(EventLog* log)
    {
        this->m_log = log; //set the log info so if the pop isn't called the destructor will record propagation
    }

    void TTDExceptionFramePopper::PopInfo()
    {
        this->m_log = nullptr; //normal pop (no exception) just clear so destructor nops
    }

    TTDRecordFunctionActionTimePopper::TTDRecordFunctionActionTimePopper(EventLog* log)
        : m_log(log), m_timer(), m_callAction(nullptr)
    {
        ;
    }

    TTDRecordFunctionActionTimePopper::~TTDRecordFunctionActionTimePopper()
    {
        double endTime = this->m_timer.Now();
        this->m_callAction->SetElapsedTime(endTime - this->m_startTime);
        this->m_log->IncrementElapsedSnapshotTime(endTime - this->m_startTime);
    }

    void TTDRecordFunctionActionTimePopper::SetCallAction(JsRTCallFunctionAction* action)
    {
        this->m_callAction = action;
    }

    double TTDRecordFunctionActionTimePopper::GetStartTime()
    {
        this->m_startTime = this->m_timer.Now();

        return this->m_startTime;
    }

    const SingleCallCounter& EventLog::GetTopCallCounter() const
    {
        AssertMsg(this->m_callStack.Count() != 0, "Empty stack!");

        return this->m_callStack.Item(this->m_callStack.Count() - 1);
    }

    SingleCallCounter& EventLog::GetTopCallCounter()
    {
        AssertMsg(this->m_callStack.Count() != 0, "Empty stack!");

        return this->m_callStack.Item(this->m_callStack.Count() - 1);
    }

    const SingleCallCounter& EventLog::GetTopCallCallerCounter() const
    {
        AssertMsg(this->m_callStack.Count() >= 2, "Empty stack!");

        return this->m_callStack.Item(this->m_callStack.Count() - 2);
    }

    int64 EventLog::GetCurrentEventTimeAndAdvance()
    {
        return this->m_eventTimeCtr++;
    }

    void EventLog::AdvanceTimeAndPositionForReplay()
    {
        this->m_eventTimeCtr++;
        this->m_currentEvent = this->m_currentEvent->GetNextEvent();

        AssertMsg(this->m_currentEvent == nullptr || this->m_eventTimeCtr <= this->m_currentEvent->GetEventTime(), "Something is out of sync.");
    }

    void EventLog::InsertEventAtHead(EventLogEntry* evnt)
    {
        evnt->SetPreviousEvent(this->m_events);

        if(this->m_events != nullptr)
        {
            this->m_events->SetNextEvent(evnt);
        }

        this->m_events = evnt;
    }

    void EventLog::UpdateComputedMode()
    {
        AssertMsg(this->m_modeStack.Count() >= 0, "Should never be empty!!!");

        TTDMode cm = TTDMode::Invalid;
        for(int32 i = 0; i < this->m_modeStack.Count(); ++i)
        {
            TTDMode m = this->m_modeStack.Item(i);
            switch(m)
            {
            case TTDMode::Disabled:
            case TTDMode::Detached:
            case TTDMode::RecordEnabled:
            case TTDMode::DebuggingEnabled:
                AssertMsg(i == 0, "One of these should always be first on the stack.");
                cm = m;
                break;
            case TTDMode::ExcludedExecution:
                AssertMsg(i != 0, "A base mode should always be first on the stack.");
                cm |= m;
                break;
            default:
                AssertMsg(false, "This mode is unknown or should never appear.");
                break;
            }
        }

        this->m_currentMode = cm;

        if(this->m_ttdContext != nullptr)
        {
            this->m_ttdContext->SetMode_TTD(this->m_currentMode);
        }
    }

    void EventLog::UnloadRetainedData()
    {
        if(this->m_lastInflateMap != nullptr)
        {
            HeapDelete(this->m_lastInflateMap);
            this->m_lastInflateMap = nullptr;
        }

        if(this->m_propertyRecordPinSet != nullptr)
        {
            this->m_propertyRecordPinSet->GetAllocator()->RootRelease(this->m_propertyRecordPinSet);
            this->m_propertyRecordPinSet = nullptr;
        }
    }

    void EventLog::DoSnapshotExtract_Helper(bool firstSnap, SnapShot** snap, TTD_LOG_TAG* logTag, TTD_IDENTITY_TAG* identityTag)
    {
        AssertMsg(this->m_ttdContext != nullptr, "We aren't actually tracking anything!!!");

        JsUtil::List<Js::Var, HeapAllocator> roots(&HeapAllocator::Instance);
        JsUtil::List<Js::ScriptContext*, HeapAllocator> ctxs(&HeapAllocator::Instance);

        ctxs.Add(this->m_ttdContext);
        this->m_ttdContext->ExtractSnapshotRoots_TTD(roots);

        this->m_snapExtractor.BeginSnapshot(this->m_threadContext, roots, ctxs, firstSnap);
        this->m_snapExtractor.DoMarkWalk(roots, ctxs, this->m_threadContext, firstSnap);

        ///////////////////////////
        //Phase 2: Evacuate marked objects
        //Allows for parallel execute and evacuate (in conjunction with later refactoring)

        this->m_snapExtractor.EvacuateMarkedIntoSnapshot(this->m_threadContext, ctxs);

        ///////////////////////////
        //Phase 3: Complete and return snapshot

        *snap = this->m_snapExtractor.CompleteSnapshot();

        ///////////////////////////
        //Get the tag information

        this->m_threadContext->TTDInfo->GetTagsForSnapshot(logTag, identityTag);
    }

    EventLog::EventLog(ThreadContext* threadContext, LPCWSTR logDir)
        : m_threadContext(threadContext), m_slabAllocator(),
        m_logInfoRootDir(nullptr), m_eventTimeCtr(0), m_runningFunctionTimeCtr(0), m_topLevelCallbackEventTime(-1), m_hostCallbackId(-1),
        m_events(nullptr), m_currentEvent(nullptr),
        m_callStack(&HeapAllocator::Instance), 
#if ENABLE_TTD_DEBUGGING
        m_isReturnFrame(false), m_isExceptionFrame(false), m_lastFrame(),
#endif
        m_modeStack(&HeapAllocator::Instance), m_currentMode(TTDMode::Disabled),
        m_ttdContext(nullptr),
        m_snapExtractor(), m_elapsedExecutionTimeSinceSnapshot(0.0),
        m_lastInflateSnapshotTime(-1), m_lastInflateMap(nullptr), m_propertyRecordPinSet(nullptr), m_propertyRecordList(&this->m_slabAllocator)
#if ENABLE_TTD_DEBUGGING_TEMP_WORKAROUND
        , BPIsSet(false)
        , BPRootEventTime(-1)
        , BPFunctionTime(0)
        , BPLoopTime(0)
        , BPLine(0)
        , BPColumn(0)
        , BPSourceContextId(0)
        , BPBreakAtNextStmtInto(false)
        , BPBreakAtNextStmtDepth(-1)
#endif
    {
        uint32 logDirPathLength = wcslen(logDir) + 1;
        wchar* pathBuff = HeapNewArrayZ(wchar, logDirPathLength);
        memcpy(pathBuff, logDir, logDirPathLength * sizeof(wchar));
        this->m_logInfoRootDir = pathBuff;

        this->m_modeStack.Add(TTDMode::Disabled);

        this->m_propertyRecordPinSet = RecyclerNew(threadContext->GetRecycler(), ReferencePinSet, threadContext->GetRecycler());
        this->m_threadContext->GetRecycler()->RootAddRef(this->m_propertyRecordPinSet);
    }

    EventLog::~EventLog()
    {
        EventLogEntry* curr = this->m_events;
        while(curr != nullptr)
        {
            EventLogEntry* tmp = curr;
            curr = curr->GetPreviousEvent();

            this->m_slabAllocator.SlabDelete<EventLogEntry>(tmp);
        }
        this->m_events = nullptr;

        this->UnloadRetainedData();

        HeapDeleteArray(wcslen(this->m_logInfoRootDir) + 1, this->m_logInfoRootDir);
    }

    void EventLog::InitForTTDRecord()
    {
        //Prepr the logging stream so it is ready for us to write into
        this->m_threadContext->TTDWriteInitializeFunction(this->m_logInfoRootDir);

        //pin all the current properties so they don't move/disappear on us
        for(Js::PropertyId pid = TotalNumberOfBuiltInProperties + 1; pid < this->m_threadContext->GetMaxPropertyId(); ++pid)
        {
            const Js::PropertyRecord* pRecord = this->m_threadContext->GetPropertyName(pid);
            this->AddPropertyRecord(pRecord);
        }
    }

    void EventLog::InitForTTDReplay()
    {
        this->ParseLogInto();

        Js::PropertyId maxPid = TotalNumberOfBuiltInProperties + 1;
        JsUtil::BaseDictionary<Js::PropertyId, NSSnapType::SnapPropertyRecord*, HeapAllocator> pidMap(&HeapAllocator::Instance);

        for(auto iter = this->m_propertyRecordList.GetIterator(); iter.IsValid(); iter.MoveNext())
        {
            maxPid = max(maxPid, iter.Current()->PropertyId);
            pidMap.AddNew(iter.Current()->PropertyId, iter.Current());
        }

        for(Js::PropertyId cpid = TotalNumberOfBuiltInProperties + 1; cpid <= maxPid; ++cpid)
        {
            NSSnapType::SnapPropertyRecord* spRecord = pidMap.LookupWithKey(cpid, nullptr);
            AssertMsg(spRecord != nullptr, "We have a gap in the sequence of propertyIds. Not sure how that happens.");

            const Js::PropertyRecord* newPropertyRecord = NSSnapType::InflatePropertyRecord(spRecord, this->m_threadContext);

            if(!this->m_propertyRecordPinSet->ContainsKey(const_cast<Js::PropertyRecord*>(newPropertyRecord)))
            {
                this->m_propertyRecordPinSet->AddNew(const_cast<Js::PropertyRecord*>(newPropertyRecord));
            }
        }
    }

    void EventLog::StartTimeTravelOnScript(Js::ScriptContext* ctx)
    {
        AssertMsg(this->m_ttdContext == nullptr, "Should only add 1 time!");

        ctx->SetMode_TTD(this->m_currentMode);
        this->m_ttdContext = ctx;

        ctx->InitializeRecordingActionsAsNeeded_TTD();
    }

    void EventLog::StopTimeTravelOnScript(Js::ScriptContext* ctx)
    {
        AssertMsg(this->m_ttdContext == ctx, "Should be enabled before we disable!");

        ctx->SetMode_TTD(TTDMode::Detached);
        this->m_ttdContext = nullptr;
    }

    void EventLog::SetGlobalMode(TTDMode m)
    {
        AssertMsg(m == TTDMode::Disabled || m == TTDMode::Detached || m == TTDMode::RecordEnabled || m == TTDMode::DebuggingEnabled, "These are the only valid global modes");

        this->m_modeStack.SetItem(0, m);
        this->UpdateComputedMode();
    }

    void EventLog::PushMode(TTDMode m)
    {
        AssertMsg(m == TTDMode::ExcludedExecution, "These are the only valid mode modifiers to push");

        this->m_modeStack.Add(m);
        this->UpdateComputedMode();
    }

    void EventLog::PopMode(TTDMode m)
    {
        AssertMsg(m == TTDMode::ExcludedExecution, "These are the only valid mode modifiers to push");
        AssertMsg(this->m_modeStack.Last() == m, "Push/Pop is not matched so something went wrong.");

        this->m_modeStack.RemoveAtEnd();
        this->UpdateComputedMode();
    }

    void EventLog::SetIntoDebuggingMode()
    {
        this->m_modeStack.SetItem(0, TTDMode::DebuggingEnabled);
        this->UpdateComputedMode();

        this->m_ttdContext->InitializeDebuggingActionsAsNeeded_TTD();
    }

    bool EventLog::ShouldPerformRecordAction() const
    {
        bool modeIsRecord = (this->m_currentMode & TTDMode::RecordEnabled) == TTDMode::RecordEnabled;
        bool inRecordableCode = (this->m_currentMode & TTDMode::ExcludedExecution) == TTDMode::Invalid;

        return modeIsRecord & inRecordableCode;
    }

    bool EventLog::ShouldPerformDebugAction() const
    {
        bool modeIsDebug = (this->m_currentMode & TTDMode::DebuggingEnabled) == TTDMode::DebuggingEnabled;
        bool inDebugableCode = (this->m_currentMode & TTDMode::ExcludedExecution) == TTDMode::Invalid;

        return modeIsDebug & inDebugableCode;
    }

    bool EventLog::IsTTDActive() const
    {
        return (this->m_currentMode & TTDMode::TTDActive) != TTDMode::Invalid;
    }

    bool EventLog::IsTTDDetached() const
    {
        return (this->m_currentMode & TTDMode::Detached) != TTDMode::Invalid;
    }

    bool EventLog::JsRTShouldTagObject(const EventLog* elog)
    {
        return (elog != nullptr) && (elog->ShouldPerformRecordAction() | elog->ShouldPerformDebugAction());
    }

    void EventLog::AddPropertyRecord(const Js::PropertyRecord* record)
    {
        this->m_propertyRecordPinSet->AddNew(const_cast<Js::PropertyRecord*>(record));
    }

    void EventLog::RecordDateTimeEvent(double time)
    {
        AssertMsg(this->ShouldPerformRecordAction(), "Mode is inconsistent!");

        DoubleEventLogEntry* devent = this->m_slabAllocator.SlabNew<DoubleEventLogEntry>(this->GetCurrentEventTimeAndAdvance(), time);
        this->InsertEventAtHead(devent);
    }

    void EventLog::RecordDateStringEvent(Js::JavascriptString* stringValue)
    {
        AssertMsg(this->ShouldPerformRecordAction(), "Mode is inconsistent!");

        LPCWSTR copyStr = this->m_slabAllocator.CopyStringInto(stringValue->GetSz());

        StringValueEventLogEntry* sevent = this->m_slabAllocator.SlabNew<StringValueEventLogEntry>(this->GetCurrentEventTimeAndAdvance(), copyStr);
        this->InsertEventAtHead(sevent);
    }

    void EventLog::ReplayDateTimeEvent(double* result)
    {
        AssertMsg(this->ShouldPerformDebugAction(), "Mode is inconsistent!");

        if(this->m_currentEvent == nullptr)
        {
            this->AbortReplayReturnToHost();
        }

        AssertMsg(this->m_currentEvent->GetEventTime() == this->m_eventTimeCtr, "Out of Sync!!!");

        DoubleEventLogEntry* devent = DoubleEventLogEntry::As(this->m_currentEvent);
        *result = devent->GetDoubleValue();

        this->AdvanceTimeAndPositionForReplay();
    }

    void EventLog::ReplayDateStringEvent(Js::ScriptContext* ctx, Js::JavascriptString** result)
    {
        AssertMsg(this->ShouldPerformDebugAction(), "Mode is inconsistent!");

        if(this->m_currentEvent == nullptr)
        {
            this->AbortReplayReturnToHost();
        }

        AssertMsg(this->m_currentEvent->GetEventTime() == this->m_eventTimeCtr, "Out of Sync!!!");

        StringValueEventLogEntry* sevent = StringValueEventLogEntry::As(this->m_currentEvent);
        LPCWSTR str = sevent->GetStringValue();
        *result = Js::JavascriptString::NewCopyBuffer(str, wcslen(str), ctx);

        this->AdvanceTimeAndPositionForReplay();
    }

    void EventLog::RecordExternalEntropyRandomEvent(uint64 seed)
    {
        AssertMsg(this->ShouldPerformRecordAction(), "Shouldn't be logging during replay!");

        UInt64EventLogEntry* uevent = this->m_slabAllocator.SlabNew<UInt64EventLogEntry>(this->GetCurrentEventTimeAndAdvance(), seed);
        this->InsertEventAtHead(uevent);
    }

    void EventLog::ReplayExternalEntropyRandomEvent(uint64* result)
    {
        AssertMsg(this->ShouldPerformDebugAction(), "Mode is inconsistent!");

        if(this->m_currentEvent == nullptr)
        {
            this->AbortReplayReturnToHost();
        }

        AssertMsg(this->m_currentEvent->GetEventTime() == this->m_eventTimeCtr, "Out of Sync!!!");

        UInt64EventLogEntry* uevent = UInt64EventLogEntry::As(this->m_currentEvent);
        *result = uevent->GetUInt64();

        this->AdvanceTimeAndPositionForReplay();
    }

    void EventLog::RecordPropertyEnumEvent(BOOL returnCode, Js::PropertyId pid, Js::PropertyAttributes attributes, Js::JavascriptString* propertyName)
    {
        AssertMsg(this->ShouldPerformRecordAction(), "Shouldn't be logging during replay!");

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
        LPCWSTR optName = returnCode ? this->m_slabAllocator.CopyStringInto(propertyName->GetSz()) : nullptr;
#else
        LPCWSTR optName = nullptr;
        if(pid == Js::Constants::NoProperty)
        {
            LPCWSTR optName = this->m_slabAllocator.CopyStringInto(propertyName->GetSz());
        }
#endif

        PropertyEnumStepEventLogEntry* eevent = this->m_slabAllocator.SlabNew<PropertyEnumStepEventLogEntry>(this->GetCurrentEventTimeAndAdvance(), returnCode, pid, attributes, optName);
        this->InsertEventAtHead(eevent);
    }

    void EventLog::ReplayPropertyEnumEvent(BOOL* returnCode, int32* newIndex, const Js::DynamicObject* obj, Js::PropertyId* pid, Js::PropertyAttributes* attributes, Js::JavascriptString** propertyName)
    {
        AssertMsg(this->ShouldPerformDebugAction(), "Mode is inconsistent!");

        if(this->m_currentEvent == nullptr)
        {
            this->AbortReplayReturnToHost();
        }

        AssertMsg(this->m_currentEvent->GetEventTime() == this->m_eventTimeCtr, "Out of Sync!!!");

        PropertyEnumStepEventLogEntry* eevent = PropertyEnumStepEventLogEntry::As(this->m_currentEvent);
        *returnCode = eevent->GetReturnCode();
        *pid = eevent->GetPropertyId();
        *attributes = eevent->GetAttributes();

        if(*returnCode)
        {
            AssertMsg(*pid != Js::Constants::NoProperty, "This is so weird we need to figure out what this means.");
            Js::PropertyString* propertyString = obj->GetScriptContext()->GetPropertyString(*pid);
            *propertyName = propertyString;

            const Js::PropertyRecord* pRecord = obj->GetScriptContext()->GetPropertyName(*pid);
            *newIndex = obj->GetDynamicType()->GetTypeHandler()->GetPropertyIndex(pRecord);
        }
        else
        {
            *propertyName = nullptr;

            *newIndex = obj->GetDynamicType()->GetTypeHandler()->GetPropertyCount();
        }

        this->AdvanceTimeAndPositionForReplay();
    }

    void EventLog::RecordSymbolCreationEvent(Js::PropertyId pid)
    {
        AssertMsg(this->ShouldPerformRecordAction(), "Shouldn't be logging during replay!");

        SymbolCreationEventLogEntry* sevent = this->m_slabAllocator.SlabNew<SymbolCreationEventLogEntry>(this->GetCurrentEventTimeAndAdvance(), pid);
        this->InsertEventAtHead(sevent);
    }

    void EventLog::ReplaySymbolCreationEvent(Js::PropertyId* pid)
    {
        AssertMsg(this->ShouldPerformDebugAction(), "Mode is inconsistent!");

        if(this->m_currentEvent == nullptr)
        {
            this->AbortReplayReturnToHost();
        }

        AssertMsg(this->m_currentEvent->GetEventTime() == this->m_eventTimeCtr, "Out of Sync!!!");

        SymbolCreationEventLogEntry* sevent = SymbolCreationEventLogEntry::As(this->m_currentEvent);
        *pid = sevent->GetPropertyId();

        this->AdvanceTimeAndPositionForReplay();
    }

    ExternalCallEventBeginLogEntry* EventLog::RecordExternalCallBeginEvent(Js::JavascriptFunction* func, int32 rootDepth, double beginTime)
    {
        AssertMsg(this->ShouldPerformRecordAction(), "Shouldn't be logging during replay!");

        ExternalCallEventBeginLogEntry* eevent = this->m_slabAllocator.SlabNew<ExternalCallEventBeginLogEntry>(this->GetCurrentEventTimeAndAdvance(), rootDepth, beginTime);

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
        eevent->SetFunctionName(func->GetDisplayName()->GetSz());
#endif

        this->InsertEventAtHead(eevent);

        return eevent;
    }

    void EventLog::RecordExternalCallEndEvent(Js::JavascriptFunction* func, int32 rootDepth, Js::Var value)
    {
        AssertMsg(this->ShouldPerformRecordAction(), "Shouldn't be logging during replay!");

        NSLogValue::ArgRetValue* retVal = this->m_slabAllocator.SlabAllocateStruct<NSLogValue::ArgRetValue>();
        NSLogValue::ExtractArgRetValueFromVar(value, retVal, this->m_slabAllocator);

        ExternalCallEventEndLogEntry* eevent = this->m_slabAllocator.SlabNew<ExternalCallEventEndLogEntry>(this->GetCurrentEventTimeAndAdvance(), rootDepth, retVal);

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
        eevent->SetFunctionName(func->GetDisplayName()->GetSz());
#endif

        this->InsertEventAtHead(eevent);
    }

    void EventLog::ReplayExternalCallEvent(Js::ScriptContext* ctx, Js::Var* result)
    {
        AssertMsg(this->ShouldPerformDebugAction(), "Mode is inconsistent!");

        if(this->m_currentEvent == nullptr)
        {
            this->AbortReplayReturnToHost();
        }
        AssertMsg(this->m_currentEvent->GetEventTime() == this->m_eventTimeCtr, "Out of Sync!!!");

        //advance the begin event item off the event list
        ExternalCallEventBeginLogEntry* eeventBegin = ExternalCallEventBeginLogEntry::As(this->m_currentEvent);
        this->AdvanceTimeAndPositionForReplay();

        //replay anything that happens when we are out of the call
        if(this->m_currentEvent->GetEventKind() == EventLogEntry::EventKind::JsRTActionTag)
        {
            this->ReplayActionLoopStep();
        }

        if(this->m_currentEvent == nullptr)
        {
            this->AbortReplayReturnToHost();
        }
        AssertMsg(this->m_currentEvent->GetEventTime() == this->m_eventTimeCtr, "Out of Sync!!!");

        //advance the end event item off the event list and get the return value
        ExternalCallEventEndLogEntry* eeventEnd = ExternalCallEventEndLogEntry::As(this->m_currentEvent);
        this->AdvanceTimeAndPositionForReplay();

        AssertMsg(eeventBegin->GetRootNestingDepth() == eeventEnd->GetRootNestingDepth(), "These should always match!!!");

        *result = NSLogValue::InflateArgRetValueIntoVar(eeventEnd->GetReturnValue(), ctx);
    }

    void EventLog::PushCallEvent(Js::FunctionBody* fbody)
    {
        AssertMsg(this->IsTTDActive(), "Should check this first.");

        if(this->ShouldPerformRecordAction() | this->ShouldPerformDebugAction())
        {
#if ENABLE_TTD_DEBUGGING
            //Clear any previous last return frame info
            this->ClearReturnAndExceptionFrames();
#endif

            this->m_runningFunctionTimeCtr++;

            SingleCallCounter cfinfo;
            cfinfo.Function = fbody;

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
            cfinfo.Name = fbody->GetExternalDisplayName();
#endif

            cfinfo.EventTime = this->m_eventTimeCtr; //don't need to advance just note what the event time was when this is called
            cfinfo.FunctionTime = this->m_runningFunctionTimeCtr;
            cfinfo.LoopTime = 0;

#if ENABLE_TTD_DEBUGGING
            cfinfo.CurrentStatementIndex = -1;
            cfinfo.CurrentStatementLoopTime = 0;

            cfinfo.LastStatementIndex = -1;
            cfinfo.LastStatementLoopTime = 0;

            cfinfo.CurrentStatementBytecodeMin = UINT32_MAX;
            cfinfo.CurrentStatementBytecodeMax = UINT32_MAX;
#endif

            this->m_callStack.Add(cfinfo);
        }
    }

    void EventLog::PopCallEvent(Js::FunctionBody* fbody, Js::Var result)
    {
        AssertMsg(this->IsTTDActive(), "Should check this first.");

        if(this->ShouldPerformRecordAction() | this->ShouldPerformDebugAction())
        {
#if ENABLE_TTD_DEBUGGING
            this->SetReturnAndExceptionFramesFromCurrent(true, false);
#endif

            this->m_runningFunctionTimeCtr++;
            this->m_callStack.RemoveAtEnd();
        }
    }

    void EventLog::PopCallEventException(bool isFirstException)
    {
        AssertMsg(this->IsTTDActive(), "Should check this first.");

        if(this->ShouldPerformRecordAction() | this->ShouldPerformDebugAction())
        {
#if ENABLE_TTD_DEBUGGING
            if(isFirstException)
            {
                this->SetReturnAndExceptionFramesFromCurrent(false, true);
            }
#endif

            this->m_runningFunctionTimeCtr++;
            this->m_callStack.RemoveAtEnd();
        }
    }

#if ENABLE_TTD_DEBUGGING
    bool EventLog::HasImmediateReturnFrame() const
    {
        return this->m_isReturnFrame;
    }

    bool EventLog::HasImmediateExceptionFrame() const
    {
        return this->m_isExceptionFrame;
    }

    const SingleCallCounter& EventLog::GetImmediateReturnFrame() const
    {
        AssertMsg(this->IsTTDActive(), "Should check this first.");
        AssertMsg(this->m_isReturnFrame, "This data is invalid if we haven't recorded a return!!!");

        return this->m_lastFrame;
    }

    const SingleCallCounter& EventLog::GetImmediateExceptionFrame() const
    {
        AssertMsg(this->IsTTDActive(), "Should check this first.");
        AssertMsg(this->m_isExceptionFrame, "This data is invalid if we haven't recorded an exception!!!");

        return this->m_lastFrame;
    }

    void EventLog::ClearReturnAndExceptionFrames()
    {
        this->m_isReturnFrame = false;
        this->m_isExceptionFrame = false;
    }

    void EventLog::SetReturnAndExceptionFramesFromCurrent(bool setReturn, bool setException)
    {
        AssertMsg(this->IsTTDActive(), "Should check this first.");
        AssertMsg(this->m_callStack.Count() != 0, "We must have pushed something in order to have an exception or return!!!");
        AssertMsg((setReturn | setException) & (!setReturn | !setException), "We can only have a return or exception -- exactly one not both!!!");

        this->m_isReturnFrame = setReturn;
        this->m_isExceptionFrame = setException;

        this->m_lastFrame = this->m_callStack.Last();
    }
#endif

    void EventLog::UpdateLoopCountInfo()
    {
        AssertMsg(this->IsTTDActive(), "Should check this first.");

        if(this->ShouldPerformRecordAction() | this->ShouldPerformDebugAction())
        {
            SingleCallCounter& cfinfo = this->m_callStack.Last();
            cfinfo.LoopTime++;
        }
    }

#if ENABLE_TTD_DEBUGGING
    bool EventLog::UpdateCurrentStatementInfo(uint bytecodeOffset)
    {
        SingleCallCounter& cfinfo = this->GetTopCallCounter();
        if((cfinfo.CurrentStatementBytecodeMin <= bytecodeOffset) & (bytecodeOffset <= cfinfo.CurrentStatementBytecodeMax))
        {
            return false;
        }
        else
        {
            Js::FunctionBody* fb = cfinfo.Function;

            int32 cIndex = fb->GetEnclosingStatementIndexFromByteCode(bytecodeOffset, true);
            AssertMsg(cIndex != -1, "Should always have a mapping.");

            //we moved to a new statement
            Js::FunctionBody::StatementMap* pstmt = fb->GetStatementMaps()->Item(cIndex);
            bool newstmt = (cIndex != cfinfo.CurrentStatementIndex && pstmt->byteCodeSpan.begin <= (int)bytecodeOffset && (int)bytecodeOffset <= pstmt->byteCodeSpan.end);
            if(newstmt)
            {
                cfinfo.LastStatementIndex = cfinfo.CurrentStatementIndex;
                cfinfo.LastStatementLoopTime = cfinfo.CurrentStatementLoopTime;

                cfinfo.CurrentStatementIndex = cIndex;
                cfinfo.CurrentStatementLoopTime = cfinfo.LoopTime;

                cfinfo.CurrentStatementBytecodeMin = (uint32)pstmt->byteCodeSpan.begin;
                cfinfo.CurrentStatementBytecodeMax = (uint32)pstmt->byteCodeSpan.end;
            }

            return newstmt;
        }
    }

    void EventLog::GetTimeAndPositionForDebugger(int64* rootEventTime, uint64* ftime, uint64* ltime, uint32* line, uint32* column, uint32* sourceId) const
    {
        AssertMsg(this->ShouldPerformDebugAction(), "This should only be executed if we are debugging.");

        const SingleCallCounter& cfinfo = this->GetTopCallCounter();

        *rootEventTime = this->m_topLevelCallbackEventTime;
        *ftime = cfinfo.FunctionTime;
        *ltime = cfinfo.LoopTime;

        ULONG srcLine = 0;
        LONG srcColumn = -1;
        const size_t startOffset = cfinfo.Function->GetStatementStartOffset(cfinfo.CurrentStatementIndex);
        cfinfo.Function->GetSourceLineFromStartOffset_TTD(startOffset, &srcLine, &srcColumn);

        *line = (uint32)srcLine;
        *column = (uint32)srcColumn;
        *sourceId = cfinfo.Function->GetSourceContextId();
    }

    bool EventLog::GetPreviousTimeAndPositionForDebugger(int64* rootEventTime, uint64* ftime, uint64* ltime, uint32* line, uint32* column, uint32* sourceId) const
    {
        AssertMsg(this->ShouldPerformDebugAction(), "This should only be executed if we are debugging.");

        const SingleCallCounter& cfinfo = this->GetTopCallCounter();

        //this always works -- even if we are at the start of the function
        *rootEventTime = this->m_topLevelCallbackEventTime;

        //check if we are at the first statement in the callback event
        if(this->m_callStack.Count() == 1 && cfinfo.LastStatementIndex == -1)
        {
            return true;
        }

        //if we are at the first statement in the function then we want the parents current
        Js::FunctionBody* fbody = nullptr;
        int32 statementIndex = -1;
        if(cfinfo.LastStatementIndex == -1)
        {
            const SingleCallCounter& cfinfoCaller = this->GetTopCallCallerCounter();
            *ftime = cfinfoCaller.FunctionTime;
            *ltime = cfinfoCaller.CurrentStatementLoopTime;

            fbody = cfinfoCaller.Function;
            statementIndex = cfinfoCaller.CurrentStatementIndex;
        }
        else
        {
            *ftime = cfinfo.FunctionTime;
            *ltime = cfinfo.LastStatementLoopTime;

            fbody = cfinfo.Function;
            statementIndex = cfinfo.LastStatementIndex;
        }

        ULONG srcLine = 0;
        LONG srcColumn = -1;
        const size_t startOffset = fbody->GetStatementStartOffset(statementIndex);
        fbody->GetSourceLineFromStartOffset_TTD(startOffset, &srcLine, &srcColumn);

        *line = (uint32)srcLine;
        *column = (uint32)srcColumn;
        *sourceId = fbody->GetSourceContextId();

        return false;
    }

    bool EventLog::GetExceptionTimeAndPositionForDebugger(int64* rootEventTime, uint64* ftime, uint64* ltime, uint32* line, uint32* column, uint32* sourceId) const
    {
        if(!this->m_isExceptionFrame)
        {
            *rootEventTime = -1;
            *ftime = 0;
            *ltime = 0;

            *line = 0;
            *column = 0;
            *sourceId = 0;

            return false;
        }
        else
        {
            *rootEventTime = this->m_topLevelCallbackEventTime;
            *ftime = this->m_lastFrame.FunctionTime;
            *ltime = this->m_lastFrame.CurrentStatementLoopTime;

            ULONG srcLine = 0;
            LONG srcColumn = -1;
            const size_t startOffset = this->m_lastFrame.Function->GetStatementStartOffset(this->m_lastFrame.CurrentStatementIndex);
            this->m_lastFrame.Function->GetSourceLineFromStartOffset_TTD(startOffset, &srcLine, &srcColumn);

            *line = (uint32)srcLine;
            *column = (uint32)srcColumn;
            *sourceId = this->m_lastFrame.Function->GetSourceContextId();

            return true;
        }
    }

    bool EventLog::GetImmediateReturnTimeAndPositionForDebugger(int64* rootEventTime, uint64* ftime, uint64* ltime, uint32* line, uint32* column, uint32* sourceId) const
    {
        if(!this->m_isReturnFrame)
        {
            *rootEventTime = -1;
            *ftime = 0;
            *ltime = 0;

            *line = 0;
            *column = 0;
            *sourceId = 0;

            return false;
        }
        else
        {
            *rootEventTime = this->m_topLevelCallbackEventTime;
            *ftime = this->m_lastFrame.FunctionTime;
            *ltime = this->m_lastFrame.CurrentStatementLoopTime;

            ULONG srcLine = 0;
            LONG srcColumn = -1;
            const size_t startOffset = this->m_lastFrame.Function->GetStatementStartOffset(this->m_lastFrame.CurrentStatementIndex);
            this->m_lastFrame.Function->GetSourceLineFromStartOffset_TTD(startOffset, &srcLine, &srcColumn);

            *line = (uint32)srcLine;
            *column = (uint32)srcColumn;
            *sourceId = this->m_lastFrame.Function->GetSourceContextId();

            return true;
        }
    }

    int64 EventLog::GetCurrentHostCallbackId() const
    {
        return this->m_hostCallbackId;
    }

    int64 EventLog::GetCurrentTopLevelEventTime() const
    {
        return this->m_topLevelCallbackEventTime;
    }

    JsRTCallbackAction* EventLog::GetEventForHostCallbackId(bool wantRegisterOp, int64 hostIdOfInterest) const
    {
        if(hostIdOfInterest == -1)
        {
            return nullptr;
        }

        for(EventLogEntry* curr = this->m_events; curr != nullptr; curr = curr->GetPreviousEvent())
        {
            if(curr->GetEventKind() == EventLogEntry::EventKind::JsRTActionTag && JsRTActionLogEntry::As(curr)->GetActionTypeTag() == JsRTActionType::CallbackOp)
            {
                JsRTCallbackAction* callbackAction = JsRTCallbackAction::As(JsRTActionLogEntry::As(curr));
                if(callbackAction->GetAssociatedHostCallbackId() == hostIdOfInterest && callbackAction->IsCreateOp() == wantRegisterOp)
                {
                    return callbackAction;
                }
            }
        }

        return nullptr;
    }

#if ENABLE_TTD_DEBUGGING_TEMP_WORKAROUND
    void EventLog::ClearBreakpointOnNextStatement()
    {
        this->BPBreakAtNextStmtInto = false;
        this->BPBreakAtNextStmtDepth = -1;
    }

    void EventLog::SetBreakpointOnNextStatement(bool into)
    {
        this->BPBreakAtNextStmtInto = into;
        this->BPBreakAtNextStmtDepth = this->m_callStack.Count();
    }

    void EventLog::BPPrintBaseVariable(Js::ScriptContext* ctx, Js::Var var, bool expandObjects)
    {
        Js::TypeId tid = Js::JavascriptOperators::GetTypeId(var);
        switch(tid)
        {
        case Js::TypeIds_Undefined:
            wprintf(L"undefined");
            break;
        case Js::TypeIds_Null:
            wprintf(L"null");
            break;
        case Js::TypeIds_Boolean:
            wprintf(Js::JavascriptBoolean::FromVar(var)->GetValue() ? L"true" : L"false");
            break;
        case Js::TypeIds_Integer:
            wprintf(L"%I32i", Js::TaggedInt::ToInt32(var));
            break;
        case Js::TypeIds_Number:
        {
            if(Js::NumberUtilities::IsNan(Js::JavascriptNumber::GetValue(var)))
            {
                wprintf(L"#Nan");
            }
            else if(!Js::NumberUtilities::IsFinite(Js::JavascriptNumber::GetValue(var)))
            {
                wprintf(L"Infinite");
            }
            else
            {
                if(floor(Js::JavascriptNumber::GetValue(var)) == Js::JavascriptNumber::GetValue(var))
                {
                    wprintf(L"%I64i", (int64)Js::JavascriptNumber::GetValue(var));
                }
                else
                {
                    wprintf(L"%.22f", Js::JavascriptNumber::GetValue(var));
                }
            }
            break;
        }
        case Js::TypeIds_Int64Number:
            wprintf(L"%I64i", Js::JavascriptInt64Number::FromVar(var)->GetValue());
            break;
        case Js::TypeIds_UInt64Number:
            wprintf(L"%I64u", Js::JavascriptUInt64Number::FromVar(var)->GetValue());
            break;
        case Js::TypeIds_String:
            wprintf(L"\"%ls\"", Js::JavascriptString::FromVar(var)->GetSz());
            break;
        case Js::TypeIds_Symbol:
        case Js::TypeIds_Enumerator:
        case Js::TypeIds_VariantDate:
        case Js::TypeIds_SIMDFloat32x4:
        case Js::TypeIds_SIMDFloat64x2:
        case Js::TypeIds_SIMDInt32x4:
            wprintf(L"Printing not supported for variable!");
            break;
        default:
        {
#if ENABLE_TTD_IDENTITY_TRACING
            if(Js::StaticType::Is(tid))
            {
                wprintf(L"static object w/o identity: {");
            }
            else
            {
                wprintf(L"object w/ identity %I64i: {", Js::DynamicObject::FromVar(var)->TTDObjectIdentityTag);
            }
#else
            wprintf(L"untagged object: {");
#endif

            Js::RecyclableObject* obj = Js::RecyclableObject::FromVar(var);
            int32 pcount = obj->GetPropertyCount();
            bool first = true;
            for(int32 i = 0; i < pcount; ++i)
            {
                Js::PropertyId propertyId = obj->GetPropertyId((Js::PropertyIndex)i);
                if(Js::IsInternalPropertyId(propertyId))
                {
                    continue;
                }

                if(!first)
                {
                    wprintf(L", ");
                }
                first = false;

                wprintf(L"%ls: ", ctx->GetPropertyName(propertyId)->GetBuffer());

                Js::Var pval = nullptr;
                Js::JavascriptOperators::GetProperty(obj, propertyId, &pval, ctx, nullptr);
                this->BPPrintBaseVariable(ctx, pval, false);
            }

            wprintf(L"}");
            break;
        }
        }
    }

    void EventLog::BPPrintVariable(Js::ScriptContext* ctx, LPCWSTR name)
    {
        Js::Var var = JsSupport::LoadPropertyHelper(name, ctx->GetGlobalObject(), false);
        if(var == nullptr)
        {
            wprintf(L"Name was not found in the global scope.\n");
            return;
        }

        wprintf(L"  -> ");
        this->BPPrintBaseVariable(ctx, var, true);
        wprintf(L"\n");
    }

    void EventLog::BPCheckAndAction(Js::ScriptContext* ctx)
    {
        AssertMsg(this->ShouldPerformDebugAction(), "This should only be executed if we are debugging.");

        const SingleCallCounter& cfinfo = this->GetTopCallCounter();

        bool bpHit = false;

        if(this->BPBreakAtNextStmtDepth != -1)
        {
            if(this->BPBreakAtNextStmtInto)
            {
                bpHit = true;
            }
            else
            {
                bpHit = this->m_callStack.Count() <= this->BPBreakAtNextStmtDepth;
            }
        }

        if(!bpHit)
        {
            ULONG srcLine = 0;
            LONG srcColumn = -1;
            const size_t startOffset = cfinfo.Function->GetStatementStartOffset(cfinfo.CurrentStatementIndex);
            cfinfo.Function->GetSourceLineFromStartOffset_TTD(startOffset, &srcLine, &srcColumn);

            bool lineMatch = (this->BPLine == (uint32)srcLine);
            bool columnMatch = (this->BPColumn == (uint32)srcColumn);
            bool srcMatch = (this->BPSourceContextId == cfinfo.Function->GetSourceContextId());

            bool etimeMatch = (this->BPRootEventTime == this->m_topLevelCallbackEventTime);
            bool ftimeMatch = (this->BPFunctionTime == cfinfo.FunctionTime);
            bool ltimeMatch = (this->BPLoopTime == cfinfo.LoopTime);

            bpHit = (lineMatch & columnMatch & srcMatch & etimeMatch & ftimeMatch & ltimeMatch);
        }

        int64 optAbortTime = 0;
        wchar_t* optAbortMsg = nullptr;
        bool continueExecution = true;

        if(bpHit)
        {
            //if we hit a breakpoint then disable future hits -- unless we re-enable in this handler
            this->BPIsSet = false; 
            this->BPRootEventTime = -1;
            this->ClearBreakpointOnNextStatement();

            //print the call stack
            int callStackPrint = min(this->m_callStack.Count(), 5);
            if(this->m_callStack.Count() != callStackPrint)
            {
                wprintf(L"...\n");
            }

            for(int32 i = this->m_callStack.Count() - callStackPrint; i < this->m_callStack.Count() - 1; ++i)
            {
                wprintf(L"%ls\n", this->m_callStack.Item(i).Function->GetDisplayName());
            }

            //print the current line information
            ULONG srcLine = 0;
            LONG srcColumn = -1;
            LPCUTF8 srcBegin = nullptr;
            LPCUTF8 srcEnd = nullptr;
            const size_t startOffset = cfinfo.Function->GetStatementStartOffset(cfinfo.CurrentStatementIndex);
            cfinfo.Function->GetSourceLineFromStartOffset(startOffset, &srcBegin, &srcEnd, &srcLine, &srcColumn);

            wprintf(L"----\n");
            wprintf(L"%ls @ ", this->m_callStack.Last().Function->GetDisplayName());
            wprintf(L"line: %u, column: %i, etime: %I64i, ftime: %I64u, ltime: %I64u\n\n", srcLine, srcColumn, this->m_topLevelCallbackEventTime, cfinfo.FunctionTime, cfinfo.LoopTime);

            while(srcBegin != srcEnd)
            {
                wprintf(L"%C", (wchar)*srcBegin);
                srcBegin++;
            }
            wprintf(L"\n\n");

            continueExecution = this->BPDbgCallback(&optAbortTime, &optAbortMsg);
        }

        if(!continueExecution)
        {
            throw TTDebuggerAbortException::CreateTopLevelAbortRequest(optAbortTime, optAbortMsg);
        }
    }
#endif
#endif

    void EventLog::ResetCallStackForTopLevelCall(int64 topLevelCallbackEventTime, int64 hostCallbackId)
    {
        AssertMsg(this->m_callStack.Count() == 0, "We should be at the top-level entry!!!");

        this->m_runningFunctionTimeCtr = 0;
        this->m_topLevelCallbackEventTime = topLevelCallbackEventTime;
        this->m_hostCallbackId = hostCallbackId;

#if ENABLE_TTD_DEBUGGING
        this->ClearReturnAndExceptionFrames();
#endif
    }

    double EventLog::GetElapsedSnapshotTime()
    {
        return this->m_elapsedExecutionTimeSinceSnapshot;
    }

    void EventLog::IncrementElapsedSnapshotTime(double addtlTime)
    {
        this->m_elapsedExecutionTimeSinceSnapshot += addtlTime;
    }

    void EventLog::AbortReplayReturnToHost()
    {
        throw TTDebuggerAbortException::CreateAbortEndOfLog(L"End of log reached -- returning to top-level.");
    }

    bool EventLog::HasDoneFirstSnapshot() const
    {
        return this->m_events != nullptr;
    }

    void EventLog::DoSnapshotExtract(bool firstSnap)
    {
        AssertMsg(this->m_ttdContext != nullptr, "We aren't actually tracking anything!!!");

        SnapShot* snap = nullptr;
        TTD_LOG_TAG logTag = TTD_INVALID_LOG_TAG;
        TTD_IDENTITY_TAG idTag = TTD_INVALID_IDENTITY_TAG;

        this->DoSnapshotExtract_Helper(firstSnap, &snap, &logTag, &idTag);

        ///////////////////////////
        //Create the event object and addi it to the log

        uint64 etime = this->GetCurrentEventTimeAndAdvance();

        SnapshotEventLogEntry* sevent = this->m_slabAllocator.SlabNew<SnapshotEventLogEntry>(etime, snap, etime, logTag, idTag);
        this->InsertEventAtHead(sevent);

        this->m_elapsedExecutionTimeSinceSnapshot = 0.0;
    }

    void EventLog::DoRtrSnapIfNeeded()
    {
        AssertMsg(this->m_ttdContext != nullptr, "We aren't actually tracking anything!!!");
        AssertMsg(this->m_currentEvent != nullptr && this->m_currentEvent->GetEventKind() == EventLogEntry::EventKind::JsRTActionTag, "Something in wrong with the event position.");
        AssertMsg(JsRTActionLogEntry::As(this->m_currentEvent)->IsRootCall(), "Something in wrong with the event position.");

        JsRTCallFunctionAction* rootCall = JsRTCallFunctionAction::As(JsRTActionLogEntry::As(this->m_currentEvent));

        if(!rootCall->HasReadyToRunSnapshotInfo())
        {
            SnapShot* snap = nullptr;
            TTD_LOG_TAG logTag = TTD_INVALID_LOG_TAG;
            TTD_IDENTITY_TAG idTag = TTD_INVALID_IDENTITY_TAG;
            this->DoSnapshotExtract_Helper(false, &snap, &logTag, &idTag);

            rootCall->SetReadyToRunSnapshotInfo(snap, logTag, idTag);
        }
    }

    int64 EventLog::FindSnapTimeForEventTime(int64 targetTime, bool* newCtxsNeeded)
    {
        *newCtxsNeeded = false;
        int64 snapTime = -1;

        for(EventLogEntry* curr = this->m_events; curr != nullptr; curr = curr->GetPreviousEvent())
        {
            if(curr->GetEventTime() <= targetTime)
            {
                if(curr->GetEventKind() == EventLogEntry::EventKind::SnapshotTag)
                {
                    snapTime = curr->GetEventTime();
                    break;
                }

                if(curr->GetEventKind() == EventLogEntry::EventKind::JsRTActionTag && JsRTActionLogEntry::As(curr)->IsRootCall())
                {
                    JsRTCallFunctionAction* rootEntry = JsRTCallFunctionAction::As(JsRTActionLogEntry::As(curr));

                    if(rootEntry->HasReadyToRunSnapshotInfo())
                    {
                        snapTime = curr->GetEventTime();
                        break;
                    }
                }
            }
        }

        //if this->m_lastInflateMap then this is the first time we have inflated (otherwise we always nullify and recreate as a pair)
        if(this->m_lastInflateMap != nullptr)
        {
            *newCtxsNeeded = (snapTime != this->m_lastInflateSnapshotTime);
        }

        return snapTime;
    }

    void EventLog::UpdateInflateMapForFreshScriptContexts()
    {
        this->m_ttdContext = nullptr;

        if(this->m_lastInflateMap != nullptr)
        {
            HeapDelete(this->m_lastInflateMap);
            this->m_lastInflateMap = nullptr;
        }
    }

    void EventLog::DoSnapshotInflate(int64 etime)
    {
        //collect anything that is dead
        this->m_threadContext->GetRecycler()->CollectNow<CollectNowForceInThread>();

        const SnapShot* snap = nullptr;
        int64 restoreEventTime = -1;
        TTD_LOG_TAG restoreLogTagCtr = TTD_INVALID_LOG_TAG;
        TTD_IDENTITY_TAG restoreIdentityTagCtr = TTD_INVALID_IDENTITY_TAG;

        for(EventLogEntry* curr = this->m_events; curr != nullptr; curr = curr->GetPreviousEvent())
        {
            if(curr->GetEventTime() == etime)
            {
                if(curr->GetEventKind() == EventLogEntry::EventKind::SnapshotTag)
                {
                    SnapshotEventLogEntry* snpEntry = SnapshotEventLogEntry::As(curr);
                    snpEntry->EnsureSnapshotDeserialized(this->m_logInfoRootDir, this->m_threadContext);

                    restoreEventTime = snpEntry->GetRestoreEventTime();
                    restoreLogTagCtr = snpEntry->GetRestoreLogTag();
                    restoreIdentityTagCtr = snpEntry->GetRestoreIdentityTag();

                    snap = snpEntry->GetSnapshot();
                }
                else
                {
                    JsRTCallFunctionAction* rootEntry = JsRTCallFunctionAction::As(JsRTActionLogEntry::As(curr));

                    SnapShot* ncSnap = nullptr;
                    rootEntry->GetReadyToRunSnapshotInfo(&ncSnap, &restoreLogTagCtr, &restoreIdentityTagCtr);
                    snap = ncSnap;

                    restoreEventTime = rootEntry->GetEventTime();
                }

                break;
            }
        }
        AssertMsg(snap != nullptr, "Log should start with a snapshot!!!");

        //
        //TODO: we currently assume a single context here which we load into the existing ctx
        //
        const UnorderedArrayList<NSSnapValues::SnapContext, TTD_ARRAY_LIST_SIZE_SMALL>& snpCtxs = snap->GetContextList();
        AssertMsg(this->m_ttdContext != nullptr, "We are assuming a single context");
        const NSSnapValues::SnapContext* sCtx = snpCtxs.GetIterator().Current();

        if(this->m_lastInflateMap != nullptr)
        {
            this->m_lastInflateMap->PrepForReInflate(snap->ContextCount(), snap->HandlerCount(), snap->TypeCount(), snap->PrimitiveCount() + snap->ObjectCount(), snap->BodyCount(), snap->EnvCount(), snap->SlotArrayCount());

            NSSnapValues::InflateScriptContext(sCtx, this->m_ttdContext, this->m_lastInflateMap);
        }
        else
        {
            this->m_lastInflateMap = HeapNew(InflateMap);
            this->m_lastInflateMap->PrepForInitialInflate(this->m_threadContext, snap->ContextCount(), snap->HandlerCount(), snap->TypeCount(), snap->PrimitiveCount() + snap->ObjectCount(), snap->BodyCount(), snap->EnvCount(), snap->SlotArrayCount());
            this->m_lastInflateSnapshotTime = etime;

            NSSnapValues::InflateScriptContext(sCtx, this->m_ttdContext, this->m_lastInflateMap);

            //We don't want to have a bunch of snapshots in memory (that will get big fast) so unload all but the current one
            for(EventLogEntry* curr = this->m_events; curr != nullptr; curr = curr->GetPreviousEvent())
            {
                if(curr->GetEventTime() != etime)
                {
                    curr->UnloadSnapshot();
                }
            }
        }

        //reset the tagged object maps before we do the inflate
        this->m_threadContext->TTDInfo->ResetTagsForRestore_TTD(restoreLogTagCtr, restoreIdentityTagCtr);
        this->m_eventTimeCtr = restoreEventTime;

        snap->Inflate(this->m_lastInflateMap, sCtx);
        this->m_lastInflateMap->CleanupAfterInflate();

        if(this->m_events != nullptr)
        {
            this->m_currentEvent = this->m_events;
            while(this->m_currentEvent->GetEventTime() != this->m_eventTimeCtr)
            {
                this->m_currentEvent = this->m_currentEvent->GetPreviousEvent();
            }

            //we want to advance to the event immediately after the snapshot as well so do that
            if(this->m_currentEvent->GetEventKind() == EventLogEntry::EventKind::SnapshotTag)
            {
                this->m_eventTimeCtr++;
                this->m_currentEvent = this->m_currentEvent->GetNextEvent();
            }

            //clear this out -- it shouldn't matter for most JsRT actions (alloc etc.) and should be reset by any call actions
            this->ResetCallStackForTopLevelCall(-1, -1);
        }
    }

    void EventLog::ReplaySingleEntry()
    {
        AssertMsg(this->ShouldPerformDebugAction(), "Mode is inconsistent!");

        if(this->m_currentEvent == nullptr)
        {
            this->AbortReplayReturnToHost();
        }

        switch(this->m_currentEvent->GetEventKind())
        {
            case EventLogEntry::EventKind::SnapshotTag:
                this->AdvanceTimeAndPositionForReplay(); //nothing to replay so we just move along
                break;
            case EventLogEntry::EventKind::JsRTActionTag:
                this->ReplayActionLoopStep(); 
                break;
            default:
                AssertMsg(false, "Either this is an invalid tag to replay directly (should be driven internally) or it is not known!!!");
        }
    }

    void EventLog::ReplayToTime(int64 eventTime)
    {
        AssertMsg(this->m_currentEvent != nullptr && this->m_currentEvent->GetEventTime() <= eventTime, "This isn't going to work.");

        //Note use of == in test as we want a specific root event not just sometime later
        while(this->m_currentEvent->GetEventTime() != eventTime)
        {
            this->ReplaySingleEntry();

            AssertMsg(this->m_currentEvent != nullptr && this->m_currentEvent->GetEventTime() <= eventTime, "Something is not lined up correctly.");
        }
    }

    void EventLog::ReplayFullTrace()
    {
        while(this->m_currentEvent != nullptr)
        {
            this->ReplaySingleEntry();
        }

        //we are at end of trace so abort to top level
        this->AbortReplayReturnToHost();
    }

    void EventLog::RecordJsRTAllocateInt(Js::ScriptContext* ctx, uint32 ival)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        JsRTNumberAllocateAction* allocEvent = this->m_slabAllocator.SlabNew<JsRTNumberAllocateAction>(etime, ctxTag, true, ival, 0.0);

        this->InsertEventAtHead(allocEvent);
    }

    void EventLog::RecordJsRTAllocateDouble(Js::ScriptContext* ctx, double dval)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        JsRTNumberAllocateAction* allocEvent = this->m_slabAllocator.SlabNew<JsRTNumberAllocateAction>(etime, ctxTag, false, 0, dval);

        this->InsertEventAtHead(allocEvent);
    }

    void EventLog::RecordJsRTVarConversion(Js::ScriptContext* ctx, Js::Var var, bool toBool, bool toNumber, bool toString)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        NSLogValue::ArgRetValue* vval = this->m_slabAllocator.SlabAllocateStruct<NSLogValue::ArgRetValue>();
        NSLogValue::ExtractArgRetValueFromVar(var, vval, this->m_slabAllocator);

        JsRTVarConvertAction* convertEvent = this->m_slabAllocator.SlabNew<JsRTVarConvertAction>(etime, ctxTag, toBool, toNumber, toString, vval);

        this->InsertEventAtHead(convertEvent);
    }

    void EventLog::RecordGetAndClearException(Js::ScriptContext* ctx)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        JsRTGetAndClearExceptionAction* exceptionEvent = this->m_slabAllocator.SlabNew<JsRTGetAndClearExceptionAction>(etime, ctxTag);

        this->InsertEventAtHead(exceptionEvent);
    }

    void EventLog::RecordGetProperty(Js::ScriptContext* ctx, Js::PropertyId pid, Js::Var var)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        NSLogValue::ArgRetValue* val = this->m_slabAllocator.SlabAllocateStruct<NSLogValue::ArgRetValue>();
        NSLogValue::ExtractArgRetValueFromVar(var, val, this->m_slabAllocator);

        JsRTGetPropertyAction* exceptionEvent = this->m_slabAllocator.SlabNew<JsRTGetPropertyAction>(etime, ctxTag, pid, val);

        this->InsertEventAtHead(exceptionEvent);
    }

    void EventLog::RecordJsRTCallbackOperation(Js::ScriptContext* ctx, bool isCancel, bool isRepeating, Js::JavascriptFunction* func, int64 createdCallbackId)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);
        TTD_LOG_TAG fTag = (func != nullptr) ? ctx->GetThreadContext()->TTDInfo->LookupTagForObject(func) : TTD_INVALID_LOG_TAG;

        JsRTCallbackAction* createAction = this->m_slabAllocator.SlabNew<JsRTCallbackAction>(etime, ctxTag, isCancel, isRepeating, this->m_hostCallbackId, fTag, createdCallbackId);

        this->InsertEventAtHead(createAction);
    }

    void EventLog::RecordCodeParse(Js::ScriptContext* ctx, bool isExpression, Js::JavascriptFunction* func, LPCWSTR srcCode)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);

        Js::FunctionBody* fb = JsSupport::ForceAndGetFunctionBody(func->GetFunctionBody());

        LPCWSTR optSrcUri = this->m_slabAllocator.CopyStringInto(fb->GetSourceContextInfo()->url);
        DWORD_PTR optDocumentID = fb->GetSourceContextId();

        LPCWSTR sourceCode = this->m_slabAllocator.CopyStringInto(srcCode);
        LPCWSTR dir = this->m_slabAllocator.CopyStringInto(this->m_logInfoRootDir);

        JsRTCodeParseAction* parseEvent = this->m_slabAllocator.SlabNew<JsRTCodeParseAction>(etime, ctxTag, isExpression, sourceCode, optDocumentID, optSrcUri, dir);

        this->InsertEventAtHead(parseEvent);
    }

    JsRTCallFunctionAction* EventLog::RecordJsRTCallFunction(Js::ScriptContext* ctx, int32 rootDepth, int64 hostCallbackId, double beginTime, Js::JavascriptFunction* func, uint32 argCount, Js::Var* args)
    {
        uint64 etime = this->GetCurrentEventTimeAndAdvance();
        TTD_LOG_TAG ctxTag = TTD_EXTRACT_CTX_LOG_TAG(ctx);
        TTD_LOG_TAG fTag = ctx->GetThreadContext()->TTDInfo->LookupTagForObject(func);

        NSLogValue::ArgRetValue* argArray = (argCount != 0) ? this->m_slabAllocator.SlabAllocateArray<NSLogValue::ArgRetValue>(argCount) : 0;
        for(uint32 i = 0; i < argCount; ++i)
        {
            Js::Var arg = args[i];
            NSLogValue::ExtractArgRetValueFromVar(arg, argArray + i, this->m_slabAllocator);
        }
        Js::Var* execArgs = (argCount != 0) ? this->m_slabAllocator.SlabAllocateArray<Js::Var>(argCount) : nullptr;

        JsRTCallFunctionAction* callEvent = this->m_slabAllocator.SlabNew<JsRTCallFunctionAction>(etime, ctxTag, rootDepth, hostCallbackId, beginTime, fTag, argCount, argArray, execArgs);

#if ENABLE_TTD_INTERNAL_DIAGNOSTICS
        callEvent->SetFunctionName(this->m_slabAllocator.CopyStringInto(func->GetDisplayName()->GetSz()));
#endif

        this->InsertEventAtHead(callEvent);

        return callEvent;
    }

    void EventLog::ReplayActionLoopStep()
    {
        AssertMsg(this->ShouldPerformDebugAction(), "Mode is inconsistent!");
        AssertMsg(this->m_currentEvent != nullptr && this->m_currentEvent->GetEventKind() == EventLogEntry::EventKind::JsRTActionTag, "Should check this first!");

        bool nextActionValid = false;
        bool nextActionRootCall = false;
        do
        {
            JsRTActionLogEntry* action = JsRTActionLogEntry::As(this->m_currentEvent);
            this->AdvanceTimeAndPositionForReplay();

            Js::ScriptContext* ctx = action->GetScriptContextForAction(this->m_threadContext);
            BEGIN_ENTER_SCRIPT(ctx, true, true, true);
            {
               action->ExecuteAction(this->m_threadContext);
            }
            END_ENTER_SCRIPT;

            nextActionValid = (this->m_currentEvent != nullptr && this->m_currentEvent->GetEventKind() == EventLogEntry::EventKind::JsRTActionTag);
            nextActionRootCall = (nextActionValid && JsRTActionLogEntry::As(this->m_currentEvent)->IsRootCall());

        } while(nextActionValid & !nextActionRootCall);
    }

    void EventLog::EmitLog()
    {
#if TTD_WRITE_JSON_OUTPUT || TTD_WRITE_BINARY_OUTPUT

        HANDLE logHandle = this->m_threadContext->TTDStreamFunctions.pfGetLogStream(this->m_logInfoRootDir, false, true);
        JSONWriter writer(logHandle, this->m_threadContext->TTDStreamFunctions.pfWriteBytesToStream, this->m_threadContext->TTDStreamFunctions.pfFlushAndCloseStream);

        writer.WriteRecordStart();
        writer.AdjustIndent(1);

        EventLogEntry::EmitEventList(this->m_events, this->m_logInfoRootDir, &writer, this->m_threadContext, NSTokens::Separator::BigSpaceSeparator);

        //if we haven't moved the properties to their serialized form them take care of it 
        if(this->m_propertyRecordList.Count() == 0)
        {
            for(auto iter = this->m_propertyRecordPinSet->GetIterator(); iter.IsValid(); iter.MoveNext())
            {
                Js::PropertyRecord* pRecord = static_cast<Js::PropertyRecord*>(iter.CurrentValue());
                NSSnapType::SnapPropertyRecord* sRecord = this->m_propertyRecordList.NextOpenEntry();

                sRecord->PropertyId = pRecord->GetPropertyId();
                sRecord->IsNumeric = pRecord->IsNumeric();
                sRecord->IsBound = pRecord->IsBound();
                sRecord->IsSymbol = pRecord->IsSymbol();

                sRecord->PropertyName = pRecord->GetBuffer();
            }
        }

        //emit the properties
        writer.WriteLengthValue(this->m_propertyRecordList.Count(), NSTokens::Separator::CommaSeparator);
        writer.WriteSequenceStart_DefaultKey(NSTokens::Separator::CommaSeparator);
        writer.AdjustIndent(1);
        bool first = true;
        for(auto iter = this->m_propertyRecordList.GetIterator(); iter.IsValid(); iter.MoveNext())
        {
            NSTokens::Separator sep = (!first) ? NSTokens::Separator::CommaAndBigSpaceSeparator : NSTokens::Separator::BigSpaceSeparator;
            NSSnapType::EmitSnapPropertyRecord(iter.Current(), &writer, sep);

            first = false;
        }
        writer.AdjustIndent(-1);
        writer.WriteSequenceEnd(NSTokens::Separator::BigSpaceSeparator);

        writer.AdjustIndent(-1);
        writer.WriteRecordEnd(NSTokens::Separator::BigSpaceSeparator);

        writer.FlushAndClose();

#endif
    }

    void EventLog::ParseLogInto()
    {
        HANDLE logHandle = this->m_threadContext->TTDStreamFunctions.pfGetLogStream(this->m_logInfoRootDir, true, false);
        JSONReader reader(logHandle, this->m_threadContext->TTDStreamFunctions.pfReadBytesFromStream, this->m_threadContext->TTDStreamFunctions.pfFlushAndCloseStream);

        reader.ReadRecordStart();

        this->m_events = EventLogEntry::ParseEventList(false, this->m_threadContext, &reader, this->m_slabAllocator);

        //parse the properties
        uint32 propertyCount = reader.ReadLengthValue(true);
        reader.ReadSequenceStart_WDefaultKey(true);
        for(uint32 i = 0; i < propertyCount; ++i)
        {
            NSSnapType::SnapPropertyRecord* sRecord = this->m_propertyRecordList.NextOpenEntry();
            NSSnapType::ParseSnapPropertyRecord(sRecord, i != 0, &reader, this->m_slabAllocator);
        }
        reader.ReadSequenceEnd();

        reader.ReadRecordEnd();
    }
}

#endif
