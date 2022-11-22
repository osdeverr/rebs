#include "process_util.h"
#include "target.h"

#include <fmt/format.h>

#include <reproc++/reproc.hpp>

#include <Windows.h>

namespace re
{
    int RunProcessOrThrow(std::string_view program_name, const std::vector<std::string>& cmdline, bool output, bool throw_on_bad_exit)
    {
        reproc::options options;
        options.redirect.parent = output;

        reproc::process process;

        auto start_ec = process.start(cmdline, options);
        if (start_ec)
        {
            RE_THROW ProcessRunException("{} failed to start: {} (ec={})", program_name, start_ec.message(), start_ec.value());
        }

        auto [exit_code, end_ec] = process.wait(reproc::infinite);

        // process.read(reproc::stream::out, );

        if (end_ec)
        {
            RE_THROW ProcessRunException("{} failed to run: {} (ec={} exit_code={})", program_name, end_ec.message(), end_ec.value(), exit_code);
        }

        if (throw_on_bad_exit && exit_code != 0)
        {
            RE_THROW ProcessRunException("{} failed: exit_code={}", program_name, exit_code);
        }

        return exit_code;
    }

    namespace
    {
        HANDLE gProcessUtilJob = NULL;
    }

    int RunProcessOrThrowWindows(std::string_view program_name, const std::vector<std::wstring>& cmdline, bool output, bool throw_on_bad_exit)
    {
        if (!gProcessUtilJob)
        {
            if (gProcessUtilJob = CreateJobObjectW(NULL, NULL))
            {
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = { 0 };

                // Configure all child processes associated with the job to terminate when the
                jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

                if (!SetInformationJobObject(gProcessUtilJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli)))
                    RE_THROW ProcessRunException("{} failed to start: failed to set Win32 job object information", program_name);
            }
            else
            {
                RE_THROW ProcessRunException("{} failed to start: failed to create Win32 job object", program_name);
            }
        }

        std::wstring args;

        for (auto& s : cmdline)
        {
            args.append(s);
            args.append(L" ");
        }

        STARTUPINFOW info = { sizeof(info) };
        PROCESS_INFORMATION process;

        if (::CreateProcessW(NULL, args.data(), nullptr, nullptr, true, CREATE_SUSPENDED, nullptr, nullptr, &info, &process))
        {
            if (!AssignProcessToJobObject(gProcessUtilJob, process.hProcess))
            {
                RE_THROW ProcessRunException("{} failed to start: failed to assign Win32 job object", program_name);
                ::CloseHandle(process.hThread);
            }

            ::ResumeThread(process.hThread);
            ::WaitForSingleObject(process.hProcess, INFINITE);

            DWORD exit_code = 0;
            ::GetExitCodeProcess(process.hProcess, &exit_code);

            if (throw_on_bad_exit && exit_code != 0)
            {
                RE_THROW ProcessRunException("{} failed: exit_code={}", program_name, exit_code);
            }

            return (int)exit_code;
        }
        else
        {
            RE_THROW ProcessRunException("{} failed to start: CreateProcessW failed", program_name);
        }
    }
}
