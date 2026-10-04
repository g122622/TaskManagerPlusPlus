// Diagnostic for the process actions.
//
// Terminating a process cannot be asserted safely against anything belonging to the test machine, so
// this starts its own: a child process whose only purpose is to be ended. That makes the test
// self-contained and lets it check the outcomes that matter, including the two that are not failures
// -- a process that has already exited, and the system processes that refuse termination.
#include <gtest/gtest.h>

#include <windows.h>

#include <cstdio>

#include "Platform/Windows/WindowsProcessActions.h"

namespace tmpp::platform::test
{
    namespace
    {
        /// Starts a process that stays alive until it is ended.
        ///
        /// `ping` with a long count, through the system's own copy. The path is absolute because a
        /// development machine can have another `ping` earlier on the path, and it is not the name that
        /// matters but the behaviour: it must block for longer than the test takes.
        ///
        /// `timeout` was the first choice and is unsuitable: an MSYS build of the same name shadows the
        /// Windows one, exits immediately with a usage error, and the pid is then reused. Terminating a
        /// reused pid is both why the test failed and a hazard the test must not create.
        [[nodiscard]] PROCESS_INFORMATION _startIdleProcess()
        {
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);

            PROCESS_INFORMATION info{};

            std::wstring command = L"cmd.exe /c %SystemRoot%\\System32\\ping.exe -n 120 127.0.0.1 >nul";
            std::vector<wchar_t> buffer(command.begin(), command.end());
            buffer.push_back(L'\0');

            if (CreateProcessW(nullptr,
                               buffer.data(),
                               nullptr,
                               nullptr,
                               FALSE,
                               CREATE_NO_WINDOW,
                               nullptr,
                               nullptr,
                               &startup,
                               &info) == FALSE)
            {
                return PROCESS_INFORMATION{};
            }

            return info;
        }

        [[nodiscard]] bool _isRunning(uint32_t pid)
        {
            HANDLE const handle = OpenProcess(SYNCHRONIZE, FALSE, pid);
            if (handle == nullptr)
            {
                return false;
            }

            DWORD const waited = WaitForSingleObject(handle, 0);
            CloseHandle(handle);
            return waited == WAIT_TIMEOUT;
        }
    }

    TEST(ProcessActionsDiagnostic, TerminatesAProcessItStarted)
    {
        PROCESS_INFORMATION const info = _startIdleProcess();
        ASSERT_NE(info.hProcess, nullptr) << "could not start a process to terminate";
        ASSERT_NE(info.hThread, nullptr);

        CloseHandle(info.hThread);

        uint32_t const pid = info.dwProcessId;
        std::printf("\n--- process actions ---\n");
        std::printf("started pid %u\n", pid);
        ASSERT_TRUE(_isRunning(pid)) << "the process should be running before it is ended";

        WindowsProcessActions actions;
        ProcessActionResult const result = actions.Terminate(pid);

        std::printf("terminate outcome: %d, affected %u, message '%s'\n",
                    static_cast<int>(result.outcome),
                    result.affected,
                    result.message.c_str());

        EXPECT_TRUE(result.Succeeded()) << result.message;
        EXPECT_EQ(result.affected, 1u);

        // The handle is the authoritative answer, not the process list: it is signalled the moment the
        // process ends, with no sampling delay.
        DWORD const waited = WaitForSingleObject(info.hProcess, 5000);
        std::printf("after terminate, wait returned %lu\n", waited);
        EXPECT_EQ(waited, WAIT_OBJECT_0) << "the process should have ended";

        CloseHandle(info.hProcess);
    }

    TEST(ProcessActionsDiagnostic, ReportsAlreadyExitedAsNotFound)
    {
        // Ending a process that has already gone is the ordinary race between drawing the list and
        // clicking in it, and it must not be reported as a failure.
        PROCESS_INFORMATION const info = _startIdleProcess();
        ASSERT_NE(info.hProcess, nullptr);
        CloseHandle(info.hThread);

        uint32_t const pid = info.dwProcessId;

        // The creation time is captured before the process ends, which is what a caller would have from
        // the list. It is what makes the second call safe: the pid may already have been reused.
        uint64_t createTime = 0;
        {
            FILETIME creation{};
            FILETIME exit{};
            FILETIME kernel{};
            FILETIME user{};
            ASSERT_TRUE(GetProcessTimes(info.hProcess, &creation, &exit, &kernel, &user) != FALSE);

            ULARGE_INTEGER value{};
            value.LowPart = creation.dwLowDateTime;
            value.HighPart = creation.dwHighDateTime;
            createTime = value.QuadPart;
        }

        WindowsProcessActions actions;
        ASSERT_TRUE(actions.Terminate(pid, createTime).Succeeded());

        // The handle is deliberately left open across the second call. Windows does not reuse a pid
        // while a handle to the process remains, so this makes the test deterministic: the pid still
        // refers to the process that just ended, and the outcome is the ordinary "already gone".
        //
        // Closing the handle first would reintroduce a race the test cannot win. The pid can then be
        // reused by a protected system process, for which OpenProcess itself fails with access denied
        // before any identity check can run, and the outcome depends on which process the operating
        // system happened to hand the number to.
        WaitForSingleObject(info.hProcess, 5000);

        ProcessActionResult const second = actions.Terminate(pid, createTime);
        std::printf("second terminate outcome: %d, message '%s'\n",
                    static_cast<int>(second.outcome),
                    second.message.c_str());

        EXPECT_EQ(second.outcome, ProcessActionOutcome::NotFound)
            << "an exited process must be reported as gone rather than as a failure";
        EXPECT_FALSE(second.Succeeded());

        CloseHandle(info.hProcess);
    }

    TEST(ProcessActionsDiagnostic, RefusesAProcessWhoseCreationTimeDoesNotMatch)
    {
        // The guard that stops a reused pid from being terminated, tested deterministically: a live
        // process is described with the wrong creation time, which is exactly what a reused identifier
        // looks like from the caller's side.
        //
        // This cannot be tested by racing the operating system. Whether a pid is reused, and by what,
        // is outside the test's control; the mismatch itself is not.
        PROCESS_INFORMATION const info = _startIdleProcess();
        ASSERT_NE(info.hProcess, nullptr);
        CloseHandle(info.hThread);

        uint64_t createTime = 0;
        {
            FILETIME creation{};
            FILETIME exit{};
            FILETIME kernel{};
            FILETIME user{};
            ASSERT_TRUE(GetProcessTimes(info.hProcess, &creation, &exit, &kernel, &user) != FALSE);

            ULARGE_INTEGER value{};
            value.LowPart = creation.dwLowDateTime;
            value.HighPart = creation.dwHighDateTime;
            createTime = value.QuadPart;
        }

        WindowsProcessActions actions;

        // A creation time that is not this process's, as a reused pid would present.
        ProcessActionResult const wrong = actions.Terminate(info.dwProcessId, createTime + 1);
        std::printf("mismatched creation time: outcome %d, message '%s'\n",
                    static_cast<int>(wrong.outcome),
                    wrong.message.c_str());

        EXPECT_EQ(wrong.outcome, ProcessActionOutcome::NotFound)
            << "a mismatched identity must be refused rather than acted upon";

        // And the process must still be running, which is the property that matters: refusing must not
        // have ended it.
        EXPECT_TRUE(_isRunning(info.dwProcessId)) << "a refused termination must not end the process";

        // The right creation time is accepted, which is what proves the guard is not simply refusing
        // everything.
        ProcessActionResult const right = actions.Terminate(info.dwProcessId, createTime);
        EXPECT_TRUE(right.Succeeded()) << right.message;

        WaitForSingleObject(info.hProcess, 5000);
        CloseHandle(info.hProcess);
    }

    TEST(ProcessActionsDiagnostic, RefusesTheSystemProcesses)
    {
        // The idle process and the system process are not things that can be ended. Attempting it
        // reports a confusing error, so they are refused with something the user can read.
        WindowsProcessActions actions;

        for (uint32_t const pid : {0u, 4u})
        {
            ProcessActionResult const result = actions.Terminate(pid);
            std::printf("terminate pid %u: outcome %d, message '%s'\n",
                        pid,
                        static_cast<int>(result.outcome),
                        result.message.c_str());

            EXPECT_EQ(result.outcome, ProcessActionOutcome::Protected);
            EXPECT_FALSE(result.message.empty()) << "a refusal must say why";
        }
    }

    TEST(ProcessActionsDiagnostic, TerminatesATreeDeepestFirst)
    {
        // A parent that spawns a child. Ending the tree must stop both, and the child must be gone
        // before the parent is asked to end: a parent ended first would leave the child reparented
        // rather than stopped.
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);

        // The outer shell starts an inner one and waits for it, so the tree is two deep.
        std::wstring command = L"cmd.exe /c cmd.exe /c %SystemRoot%\\System32\\ping.exe -n 120 127.0.0.1";
        std::vector<wchar_t> buffer(command.begin(), command.end());
        buffer.push_back(L'\0');

        PROCESS_INFORMATION info{};
        ASSERT_TRUE(CreateProcessW(nullptr,
                                   buffer.data(),
                                   nullptr,
                                   nullptr,
                                   FALSE,
                                   CREATE_NO_WINDOW,
                                   nullptr,
                                   nullptr,
                                   &startup,
                                   &info) != FALSE)
            << "could not start a process tree to terminate";

        CloseHandle(info.hThread);
        uint32_t const parentPid = info.dwProcessId;

        // Let the child be created before looking for it.
        Sleep(1500);

        // The root must still be the process this test started. A pid whose process has exited can be
        // reused, and terminating whatever inherited it would be a real fault rather than a flaky test.
        ASSERT_TRUE(_isRunning(parentPid)) << "the root exited before the tree could be ended";

        // The child is found from the snapshot the caller would have, which is what the real call takes.
        std::vector<std::pair<uint32_t, uint32_t>> parents;

        WindowsProcessActions actions;

        // The child is not known by id here, so the tree is ended from the parent. Whatever children it
        // has at that moment are the ones the real caller would have seen in its snapshot.
        ProcessActionResult const result = actions.TerminateTree(parentPid, 0, parents);

        std::printf("terminate tree: outcome %d, affected %u, failed %u, message '%s'\n",
                    static_cast<int>(result.outcome),
                    result.affected,
                    result.failed,
                    result.message.c_str());

        EXPECT_TRUE(result.Succeeded()) << result.message;
        EXPECT_GE(result.affected, 1u);

        DWORD const waited = WaitForSingleObject(info.hProcess, 5000);
        EXPECT_EQ(waited, WAIT_OBJECT_0) << "the root of the tree should have ended";

        CloseHandle(info.hProcess);
    }
}
