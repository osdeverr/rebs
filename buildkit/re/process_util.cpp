#include <boost/process.hpp>
#include <boost/process/async.hpp>

#include "process_util.h"
#include "target.h"

#include <fmt/format.h>

#ifdef WIN32
#include <Windows.h>
#endif

namespace re
{
    int RunProcessOrThrow(std::string_view program_name, std::vector<std::string> cmdline, bool output, bool throw_on_bad_exit, std::optional<std::string_view> working_directory)
    {
        /*
        // TRACE: Remove later!
        fmt::print(" # Running process [{}]:", program_name);

        for(auto& arg : cmdline)
            fmt::print(" {}", arg);

        fmt::print("\n");
        */

        std::error_code start_ec;

        auto path = boost::process::search_path(cmdline[0].data());
        cmdline.erase(cmdline.begin());

        boost::process::child child{
            path,
            boost::process::args = cmdline,
            start_ec,
            boost::process::start_dir = working_directory ? working_directory->data() : "."
        };

        if (start_ec)
            RE_THROW ProcessRunException("{} failed to start: {} (ec={})", program_name, start_ec.message(), start_ec.value());
        
        std::error_code end_ec;
        child.wait(end_ec);

        auto exit_code = child.exit_code();

        if (throw_on_bad_exit && exit_code != 0)
        {
            RE_THROW ProcessRunException("{} failed: exit_code={}", program_name, exit_code);
        }

        if (end_ec)
        {
            RE_THROW ProcessRunException("{} failed to run: {} (ec={} exit_code={})", program_name, end_ec.message(), end_ec.value(), exit_code);
        }

        return exit_code;
    }

#ifdef WIN32
    namespace
    {
        HANDLE gProcessUtilJob = NULL;
    }

    int RunProcessOrThrowWindows(std::string_view program_name, std::vector<std::wstring> cmdline, bool output, bool throw_on_bad_exit, std::optional<std::wstring_view> working_directory)
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

        if (::CreateProcessW(NULL, args.data(), nullptr, nullptr, true, CREATE_SUSPENDED, nullptr, working_directory ? working_directory->data() : nullptr, &info, &process))
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
#endif
}
