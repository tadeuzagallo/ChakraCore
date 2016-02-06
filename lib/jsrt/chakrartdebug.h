// Copyright (C) Microsoft. All rights reserved.

/// \mainpage Chakra Hosting Debugging API Reference
///
/// Chakra is Microsoft's JavaScript engine. It is an integral part of Internet Explorer but can
/// also be hosted independently by other applications. This reference describes the APIs available
/// to applications to debug JavaScript.

/// \file
/// \brief The Chakra hosting debugging API.
///
/// This file contains a flat C API layer. This is the API exported by ChakraCore.dll.

#ifdef _MSC_VER
#pragma once
#endif  // _MSC_VER

#ifndef _CHAKRARTDEBUG_H_
#define _CHAKRARTDEBUG_H_

    /// <summary>
    ///     Debug events reported from ChakraCore engine.
    /// </summary>
    typedef enum _JsDiagDebugEvent
    {
        /// <summary>
        ///     Indicates a break due to a breakpoint or debugger statement.
        /// </summary>
        JsDiagDebugEventBreak = 0,
        /// <summary>
        ///     Indicates a new script being compiled, this includes new source, eval, new function.
        /// </summary>
        JsDiagDebugEventSourceCompilation = 1,
        /// <summary>
        ///     Indicates compile error for a script.
        /// </summary>
        JsDiagDebugEventCompileError = 2,
        /// <summary>
        ///     Indicates a sync break.
        /// </summary>
        JsDiagDebugEventAsyncBreak = 3,
        /// <summary>
        ///     Indicates a runtime script exception.
        /// </summary>
        JsDiagDebugEventRuntimeException = 4,
    } JsDiagDebugEvent;


    /// <summary>
    ///     User implemented callback routine for debug events
    /// </summary>
    /// <remarks>
    ///     Use <c>JsDiagStartDebugging</c> to register this callback.
    /// </remarks>
    /// <param name="debugEvent">The type of JsDiagDebugEvent event.</param>
    /// <param name="eventData">Additional data related to the debug event</param>
    /// <param name="callbackState">The state passed to <c>JsDiagStartDebugging</c>.</param>
    typedef void (CALLBACK * JsDiagDebugEventCallback)(_In_ JsDiagDebugEvent debugEvent, _In_ JsValueRef eventData, _In_opt_ void* callbackState);


    /// <summary>
    ///     Starts debugging in the current runtime
    /// </summary>
    /// <param name="runtimeHandle">Runtime to put into debug mode.</param>
    /// <param name="debugEventCallback">Registers a callback to be called on every JsDiagDebugEvent.</param>
    /// <param name="callbackState">User provided state that will be passed back to the callback.</param>
    /// <returns>
    ///     The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.
    /// </returns>
    STDAPI_(JsErrorCode)
        JsDiagStartDebugging(
        _In_ JsRuntimeHandle runtimeHandle,
        _In_ JsDiagDebugEventCallback debugEventCallback,
        _In_opt_ void* callbackState);


    /// <summary>
    ///     Requests the VM to break as soon as possible
    /// </summary>
    /// <param name="runtimeHandle">Runtime to request break, should be in debug mode.</param>
    /// <returns>
    ///     The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.
    /// </returns>
    STDAPI_(JsErrorCode)
        JsDiagRequestAsyncBreak(
            _In_ JsRuntimeHandle runtimeHandle);


    /// <summary>
    ///     List all active breakpoint in the runtime
    /// </summary>
    /// <param name="breakPoints">Array of breakpoints</param>
    /// <remarks>
    ///     <para>
    ///     [{
    ///         "breakpointId" : 1,
    ///         "scriptId" : 1,
    ///         "line" : 0,
    ///         "column" : 62
    ///     }]
    ///     </para>
    /// </remarks>
    /// <returns>
    ///     The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.
    /// </returns>
    STDAPI_(JsErrorCode)
        JsDiagGetBreakpoints(
            _Out_ JsValueRef *breakPoints);

    /// <summary>
    ///     Sets breakpoint in the specified script at a location
    /// </summary>
    /// <param name="scriptId">Id of script from JsDiagGetScripts or JsDiagGetSource to but breakpoint</param>
    /// <param name="lineNumber">0 based line number to put breakpoint</param>
    /// <param name="columnNumber">0 based column number to put breakpoint</param>
    /// <param name="breakpointId">Breakpoint id if success</param>
    /// <returns>
    ///     The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.
    /// </returns>
    STDAPI_(JsErrorCode)
        JsDiagSetBreakpoint(
            _In_ unsigned int scriptId,
            _In_ unsigned int lineNumber,
            _In_ unsigned int columnNumber,
            _Out_opt_ unsigned int *breakpointId);


    /// <summary>
    ///     Remove a breakpoint
    /// </summary>
    /// <param name="breakpointId">Breakpoint id returned from JsDiagSetBreakpoint</param>
    /// <returns>
    ///     The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.
    /// </returns>
    STDAPI_(JsErrorCode)
        JsDiagRemoveBreakpoint(
            _In_ unsigned int breakpointId);


    /// <summary>
    ///     Break on Exception types
    /// </summary>
    typedef enum _JsDiagBreakOnExceptionType
    {
        /// <summary>
        ///     Don't break on exception
        /// </summary>
        JsDiagBreakOnExceptionTypeNone = 0,
        /// <summary>
        ///     Only break on uncaught exceptions
        /// </summary>
        JsDiagBreakOnExceptionTypeUncaught = 1,
        /// <summary>
        ///     Break on all exceptions (first chance exception)
        /// </summary>
        JsDiagBreakOnExceptionTypeAll = 2
    } JsDiagBreakOnExceptionType;

    /// <summary>
    ///     Sets break on exception handling
    /// </summary>
    /// <param name="exceptionType">Type of JsDiagBreakOnExceptionType to set</param>
    /// <returns>
    ///     The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.
    /// </returns>
    STDAPI_(JsErrorCode)
        JsDiagSetBreakOnException(
            _In_ JsDiagBreakOnExceptionType exceptionType);


    /// <summary>
    ///     Gets break on exception setting
    /// </summary>
    /// <param name="exceptionType">Value of JsDiagBreakOnExceptionType</param>
    /// <returns>
    ///     The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.
    /// </returns>
    STDAPI_(JsErrorCode)
        JsDiagGetBreakOnException(
            _Out_ JsDiagBreakOnExceptionType* exceptionType);

    /// <summary>
    ///     Steping types
    /// </summary>
    typedef enum _JsDiagResumeType
    {
        /// <summary>
        ///     Perform a step operation to next statement.
        /// </summary>
        JsDiagResumeTypeStepIn = 0,
        /// <summary>
        ///     Perform a step out from the current function.
        /// </summary>
        JsDiagResumeTypeStepOut = 1,
        /// <summary>
        ///     Perform a single step over after a debug break if the next statement is a function call, else behaves as a stepin.
        /// </summary>
        JsDiagResumeTypeStepOver = 2
    } JsDiagResumeType;

    /// <summary>
    ///     Resume execution in the VM after a debug break or exception
    /// </summary>
    /// <remarks>
    ///     Requires to be at a debug break.
    /// </remarks>
    /// <param name="resumeType">Type of JsDiagResumeType</param>
    /// <returns>
    ///     The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.
    /// </returns>

    STDAPI_(JsErrorCode)
        JsDiagResume(
            _In_ JsDiagResumeType resumeType);


    /// <summary>
    ///     Gets list of scripts
    /// </summary>
    /// <param name="scriptsArray">Array of script objects</param>
    /// <remarks>
    ///     <para>
    ///     [{
    ///         "scriptId" : 1,
    ///         "fileName" : "c:\\Test\\Test.js",
    ///         "lineCount" : 12,
    ///         "sourceLength" : 195,
    ///         "handle" : 3
    ///     }]
    ///     </para>
    /// </remarks>
    /// <returns>
    ///     The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.
    /// </returns>
    STDAPI_(JsErrorCode)
        JsDiagGetScripts(
            _Out_ JsValueRef *scriptsArray);


    /// <summary>
    ///     Gets source for a specific script identified by scriptId from JsDiagGetScripts
    /// </summary>
    /// <param name="scriptId">Id of the script</param>
    /// <param name="source">Source object</param>
    /// <remarks>
    ///     <para>
    ///     {
    ///         "scriptId" : 1,
    ///         "fileName" : "c:\\Test\\Test.js",
    ///         "lineCount" : 12,
    ///         "sourceLength" : 15154,
    ///         "source" : "var x = 1;"
    ///     }
    ///     </para>
    /// </remarks>
    /// <returns>
    ///     The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.
    /// </returns>
    STDAPI_(JsErrorCode)
        JsDiagGetSource(
            _In_ unsigned int scriptId,
            _Out_ JsValueRef *source);


    /// <summary>
    ///     Gets the source information for a function object
    /// </summary>
    /// <param name="value">JavaScript function.</param>
    /// <param name="funcInfo">Function info, scriptId, start line, start column, line numnber of first statement, column number of first statement</param>
    /// <remarks>
    ///     <para>
    ///     {
    ///         "scriptId" : 1,
    ///         "fileName" : "c:\\Test\\Test.js",
    ///         "line" : 1,
    ///         "column" : 2,
    ///         "stmtStartLine" : 0,
    ///         "stmtStartColumn" : 62
    ///     }
    ///     </para>
    /// </remarks>
    /// <returns>
    ///     The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.
    /// </returns>
    STDAPI_(JsErrorCode)
      JsDiagGetFunctionPosition(
        _In_ JsValueRef value,
        _Out_ JsValueRef *funcInfo);


    /// <summary>
    ///     Gets the stack trace information
    /// </summary>
    /// <param name="stackTrace">Stack trace information</param>
    /// <remarks>
    ///     <para>
    ///     [{
    ///        "index" : 0,
    ///        "scriptId" : 1,
    ///        "fileName" : "c:\\Test\\Test.js",
    ///        "line" : 0,
    ///        "column" : 62,
    ///        "sourceText" : "var x = 1",
    ///        "functionHandle" : 2,
    ///        "scriptHandle" : 3,
    ///        "handle" : 1
    ///    }]
    ///    </para>
    /// </remarks>
    /// <returns>
    ///     The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.
    /// </returns>
    STDAPI_(JsErrorCode)
        JsDiagGetStacktrace(
            _Out_ JsValueRef *stackTrace);


    /// <summary>
    ///     Gets the list of properties corresponding to the frame
    /// </summary>
    /// <param name="stackFrameHandle">Handle of stack frame from JsDiagGetStacktrace</param>
    /// <param name="properties">Object of properties array (properties, scopes and globals)</param>
    /// <remarks>
    ///     <para>
    ///     {
    ///         "exception" : {
    ///             "name" : "{exception}",
    ///             "type" : "object",
    ///             "display" : "'a' is undefined",
    ///             "className" : "Error",
    ///             "propertyAttributes" : 1,
    ///             "handle" : 307
    ///         }
    ///         "arguments" : {
    ///             "name" : "arguments",
    ///             "type" : "object",
    ///             "display" : "{...}",
    ///             "className" : "Object",
    ///             "propertyAttributes" : 1,
    ///             "handle" : 190
    ///         },
    ///         "returnValue" : {
    ///             "name" : "[Return value]",
    ///             "type" : "undefined",
    ///             "propertyAttributes" : 0,
    ///             "handle" : 192
    ///         },
    ///         "functionCallsReturn" : [{
    ///                 "name" : "[foo1 returned]",
    ///                 "type" : "number",
    ///                 "value" : 1,
    ///                 "propertyAttributes" : 2,
    ///                 "handle" : 191
    ///             }
    ///         ],
    ///         "locals" : [],
    ///         "scopes" : [{
    ///                 "index" : 0,
    ///                 "handle" : 193
    ///             }
    ///         ],
    ///         "globals" : {
    ///             "handle" : 194
    ///         }
    ///     }
    ///     </para>
    /// </remarks>
    /// <returns>
    ///     The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.
    /// </returns>
    STDAPI_(JsErrorCode)
        JsDiagGetStackProperties(
            _In_ unsigned int stackFrameHandle,
            _Out_ JsValueRef *properties);


    /// <summary>
    ///     Gets the list of properties corresponding to the scope, global or object
    /// </summary>
    /// <param name="handlesArray">Handles of scope, globals or object</param>
    /// <param name="propertiesObject">Array of properties</param>
    /// <remarks>
    ///     <para>
    ///     {
    ///         "112" : {
    ///             "properties" : [{
    ///                     "name" : "__proto__",
    ///                     "type" : "object",
    ///                     "display" : "{...}",
    ///                     "className" : "Object",
    ///                     "propertyAttributes" : 1,
    ///                     "handle" : 156
    ///                 }
    ///             ],
    ///             "debuggerOnlyProperties" : [{
    ///                     "name" : "[Map]",
    ///                     "type" : "string",
    ///                     "value" : "size = 0",
    ///                     "propertyAttributes" : 2,
    ///                     "handle" : 157
    ///                 }
    ///             ]
    ///         }
    ///     }
    ///     </para>
    /// </remarks>
    /// <returns>
    ///     The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.
    /// </returns>
    STDAPI_(JsErrorCode)
        JsDiagGetProperties(
            _In_ JsValueRef handlesArray,
            _Out_ JsValueRef *propertiesObject);


    /// <summary>
    ///     Get the objects corresponding to handles
    /// </summary>
    /// <param name="handlesArray">Array of handles</param>
    /// <param name="valuesObject">Collection of objects as property bag based on handles</param>
    /// <remarks>
    ///     <para>
    ///     {
    ///        "2" : {
    ///            "scriptId" : 24,
    ///            "line" : 1,
    ///            "column" : 63,
    ///            "name" : "foo",
    ///            "inferredName" : "foo",
    ///            "type" : "function",
    ///            "handle" : 2
    ///        },
    ///        "3" : {
    ///            "scriptId" : 24,
    ///            "fileName" : "c:\\nodejs\\Test\\Test.js",
    ///            "lineCount" : 8,
    ///            "sourceLength" : 137,
    ///            "handle" : 3
    ///        },
    ///        "20" : {
    ///            "name" : "this",
    ///            "type" : "object",
    ///            "display" : "{...}",
    ///            "className" : "Object",
    ///            "propertyAttributes" : 1,
    ///            "handle" : 20
    ///        }
    ///    }
    ///    </para>
    /// </remarks>
    /// <returns>
    ///     The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.
    /// </returns>
    STDAPI_(JsErrorCode)
        JsDiagLookupHandles(
            _In_ JsValueRef handlesArray,
            _Out_ JsValueRef *valuesObject);


    /// <summary>
    ///     Evaluate a script on given frame
    /// </summary>
    /// <param name="stackFrameIndex">Index of stack frame on which to evaluate the script</param>
    /// <param name="evalResult">Result of script</param>
    /// <remarks>
    ///    <para>
    ///    {
    ///        "name" : "this",
    ///        "type" : "object",
    ///        "display" : "{...}",
    ///        "className" : "Object",
    ///        "propertyAttributes" : 1,
    ///        "handle" : 18
    ///    }
    ///    </para>
    /// </remarks>
    /// <returns>
    ///     The code <c>JsNoError</c> if the operation succeeded, a failure code otherwise.
    /// </returns>
    STDAPI_(JsErrorCode)
        JsDiagEvaluate(
            _In_ const wchar_t *script,
            _In_ unsigned int stackFrameIndex,
            _Out_ JsValueRef *evalResult);

#endif // _CHAKRARTDEBUG_H_