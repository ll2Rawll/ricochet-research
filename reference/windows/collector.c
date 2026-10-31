/* SPDX-License-Identifier: MIT
 * Original Windows collector for the reference driver, not for RICOCHET.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "protocol.h"

static volatile LONG g_stop;

static BOOL WINAPI RlConsoleHandler(DWORD event)
{
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        InterlockedExchange(&g_stop, 1);
        return TRUE;
    }
    return FALSE;
}

static DWORD WINAPI RlWorker(void *context)
{
    UNREFERENCED_PARAMETER(context);
    return 0;
}

static void RlError(const char *operation)
{
    fprintf(stderr, "%s failed: Windows error %lu\n", operation, GetLastError());
}

static BOOL RlWaitForSelfActivity(HANDLE handle, const char *operation)
{
    DWORD result = WaitForSingleObject(handle, 5000);
    if (result == WAIT_OBJECT_0) return TRUE;
    if (result == WAIT_FAILED) RlError(operation);
    else fprintf(stderr, "%s did not finish within 5 seconds.\n", operation);
    return FALSE;
}

static BOOL RlSelfActivity(void)
{
    HANDLE thread;
    HMODULE module;
    WCHAR executable[32768];
    WCHAR command[32784];
    DWORD length;
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    BOOL ok = TRUE;

    /* Exercise only our own thread, a system DLL load, and a short-lived child. */
    thread = CreateThread(NULL, 0, RlWorker, NULL, 0, NULL);
    if (thread == NULL) { RlError("CreateThread"); ok = FALSE; }
    else {
        if (!RlWaitForSelfActivity(thread, "Self-test thread")) ok = FALSE;
        CloseHandle(thread);
    }

    module = LoadLibraryExW(L"version.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == NULL) { RlError("LoadLibraryExW"); ok = FALSE; }
    else FreeLibrary(module);

    length = GetModuleFileNameW(NULL, executable, (DWORD)_countof(executable));
    if (length == 0 || length >= _countof(executable)) {
        fprintf(stderr, "Cannot obtain an untruncated collector path.\n");
        return FALSE;
    }
    if (_snwprintf_s(command, _countof(command), _TRUNCATE,
                     L"\"%ls\" --child", executable) < 0) {
        fprintf(stderr, "Collector child command exceeds its buffer.\n");
        return FALSE;
    }
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    if (!CreateProcessW(executable, command, NULL, NULL, FALSE,
                         CREATE_NO_WINDOW, NULL, NULL, &startup, &process)) {
        RlError("CreateProcessW");
        ok = FALSE;
    } else {
        if (!RlWaitForSelfActivity(process.hProcess, "Self-test child")) ok = FALSE;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    return ok;
}

static BOOL RlRead(HANDLE device, uint64_t session_id, uint64_t *last_sequence,
                   uint32_t *event_count)
{
    RL_READ_REQUEST request = {
        RL_PROTOCOL_VERSION, sizeof(RL_READ_REQUEST), RL_MAX_BATCH_EVENTS, 0
    };
    RL_BATCH batch;
    DWORD returned = 0;
    uint32_t index;

    ZeroMemory(&batch, sizeof(batch));
    if (!DeviceIoControl(device, RL_IOCTL_READ, &request, sizeof(request),
                          &batch, sizeof(batch), &returned, NULL)) {
        RlError("READ");
        return FALSE;
    }
    if (returned < RL_BATCH_HEADER_SIZE || batch.version != RL_PROTOCOL_VERSION ||
        batch.event_count > RL_MAX_BATCH_EVENTS || batch.reserved != 0 ||
        batch.session_id != session_id || batch.next_sequence == 0 ||
        returned != RL_BATCH_BYTES(batch.event_count) || returned != batch.size) {
        fprintf(stderr, "Rejected malformed driver batch.\n");
        return FALSE;
    }
    /* Validate the whole batch before outputting any of its records. */
    for (index = 0; index < batch.event_count; ++index) {
        RL_EVENT *event = &batch.events[index];
        uint64_t previous = index == 0 ? *last_sequence : batch.events[index - 1].sequence;
        if (event->sequence <= previous || event->sequence >= batch.next_sequence ||
            event->flags != 0 || event->kind < RL_EVENT_SESSION_START ||
            event->kind > RL_EVENT_IMAGE_LOAD) {
            fprintf(stderr, "Rejected invalid event sequence or kind.\n");
            return FALSE;
        }
    }
    printf("{\"schema\":\"ricochet-reference-kernel/1\",\"type\":\"batch\","
           "\"session_id\":%" PRIu64 ",\"event_count\":%u,"
           "\"dropped_events\":%" PRIu64 ",\"next_sequence\":%" PRIu64 "}\n",
           batch.session_id, batch.event_count, batch.dropped_events, batch.next_sequence);
    for (index = 0; index < batch.event_count; ++index) {
        RL_EVENT *event = &batch.events[index];
        printf("{\"schema\":\"ricochet-reference-kernel/1\",\"type\":\"event\","
               "\"session_id\":%" PRIu64 ",\"sequence\":%" PRIu64 ","
               "\"time_100ns\":%" PRIu64 ",\"process_id\":%" PRIu64 ","
               "\"thread_id\":%" PRIu64 ",\"related_process_id\":%" PRIu64 ","
               "\"image_size\":%" PRIu64 ",\"kind\":%u,\"flags\":%u}\n",
               batch.session_id, event->sequence, event->time_100ns,
               event->process_id, event->thread_id, event->related_process_id,
               event->image_size, event->kind, event->flags);
        *last_sequence = event->sequence;
    }
    fflush(stdout);
    *event_count = batch.event_count;
    return !ferror(stdout);
}

int wmain(int argc, WCHAR **argv)
{
    HANDLE device;
    RL_REQUEST request = {RL_PROTOCOL_VERSION, sizeof(RL_REQUEST)};
    RL_SESSION session;
    DWORD returned = 0;
    ULONG seconds = 3;
    WCHAR *end = NULL;
    ULONGLONG deadline;
    uint64_t last_sequence = 0;
    uint32_t event_count = 0;
    unsigned int drain;
    BOOL ok = TRUE;

    if (argc == 2 && wcscmp(argv[1], L"--child") == 0) return 0;
    if (argc == 2) {
        errno = 0;
        seconds = wcstoul(argv[1], &end, 10);
        if (errno != 0 || end == argv[1] || *end != L'\0' || seconds < 1 || seconds > 60) {
            fprintf(stderr, "Usage: collector.exe [seconds: 1..60]\n");
            return 2;
        }
    } else if (argc != 1) {
        fprintf(stderr, "Usage: collector.exe [seconds: 1..60]\n");
        return 2;
    }

    device = CreateFileW(RL_DEVICE_PATH, GENERIC_READ | GENERIC_WRITE, 0,
                          NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (device == INVALID_HANDLE_VALUE) {
        RlError("Open reference device (requires installed driver and elevation)");
        return 1;
    }
    ZeroMemory(&session, sizeof(session));
    if (!DeviceIoControl(device, RL_IOCTL_START, &request, sizeof(request),
                          &session, sizeof(session), &returned, NULL)) {
        RlError("START");
        CloseHandle(device);
        return 1;
    }
    if (returned != sizeof(session) || session.size != sizeof(session) ||
        session.version != RL_PROTOCOL_VERSION || session.session_id == 0 ||
        session.target_process_id != GetCurrentProcessId()) {
        fprintf(stderr, "Rejected malformed session response.\n");
        CloseHandle(device);
        return 1;
    }
    printf("{\"schema\":\"ricochet-reference-kernel/1\",\"type\":\"session\","
           "\"session_id\":%" PRIu64 ",\"target_process_id\":%" PRIu64 "}\n",
           session.session_id, session.target_process_id);
    SetConsoleCtrlHandler(RlConsoleHandler, TRUE);
    ok = RlSelfActivity();
    deadline = GetTickCount64() + ((ULONGLONG)seconds * 1000);
    while (!InterlockedCompareExchange(&g_stop, 0, 0) && GetTickCount64() < deadline) {
        if (!RlRead(device, session.session_id, &last_sequence, &event_count)) {
            ok = FALSE;
            break;
        }
        if (event_count < RL_MAX_BATCH_EVENTS) Sleep(100);
    }
    /* Bounded drain. STOP may discard events produced after this drain. */
    for (drain = 0; drain < RL_RING_CAPACITY / RL_MAX_BATCH_EVENTS + 1; ++drain) {
        if (!RlRead(device, session.session_id, &last_sequence, &event_count)) {
            ok = FALSE;
            break;
        }
        if (event_count == 0) break;
    }
    if (!DeviceIoControl(device, RL_IOCTL_STOP, &request, sizeof(request),
                          NULL, 0, &returned, NULL)) {
        RlError("STOP");
        ok = FALSE;
    } else {
        printf("{\"schema\":\"ricochet-reference-kernel/1\",\"type\":\"stop\","
               "\"session_id\":%" PRIu64 "}\n", session.session_id);
    }
    SetConsoleCtrlHandler(RlConsoleHandler, FALSE);
    CloseHandle(device);
    if (fflush(stdout) != 0 || ferror(stdout)) ok = FALSE;
    return ok ? 0 : 1;
}
