#include "process_util.h"
#include "target.h"

#include <fmt/format.h>

#include <reproc++/reproc.hpp>

#ifdef WIN32
#include <Windows.h>
#endif

namespace re
{

// run_target->module, exe_path, run_args, true, false, working_dir
#ifdef WIN32
    int RunProcessOrThrow(std::string_view program_name, const fs::path &path, std::vector<std::string> cmdline,
                          bool output, bool throw_on_bad_exit, std::optional<fs::path> working_directory)
    {
        HANDLE hJob = NULL;
        HANDLE hProcess = NULL;
        HANDLE hThread = NULL;

        auto finish = [&]() {
            if (hJob)
            {
                ::CloseHandle(hJob);
                hJob = NULL;
            }

            if (hProcess)
            {
                ::CloseHandle(hProcess);
                hProcess = NULL;
            }

            if (hThread)
            {
                ::CloseHandle(hThread);
                hThread = NULL;
            }
        };

        try
        {
            hJob = CreateJobObjectW(NULL, NULL);
            if (!hJob)
                RE_THROW ProcessRunException("{} failed to start: failed to create Win32 job object", program_name);

            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
            jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

            if (!SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli)))
                RE_THROW ProcessRunException("{} failed to start: failed to set Win32 job object information",
                                             program_name);

            if (!path.empty())
                cmdline.insert(cmdline.begin(), path.u8string());

            std::wstring args;
            for (auto &s : cmdline)
            {
                args.append(fs::path{s}.wstring());
                args.append(L" ");
            }

            STARTUPINFOW info = {sizeof(info)};
            PROCESS_INFORMATION pi;

            if (::CreateProcessW(NULL, args.data(), nullptr, nullptr, true,
                                 CREATE_SUSPENDED | CREATE_BREAKAWAY_FROM_JOB, nullptr,
                                 working_directory ? working_directory->wstring().c_str() : nullptr, &info, &pi))
            {
                hProcess = pi.hProcess;
                hThread = pi.hThread;

                if (!AssignProcessToJobObject(hJob, pi.hProcess))
                    RE_THROW ProcessRunException("{} failed to start: failed to assign Win32 job object", program_name);

                ::ResumeThread(hThread);
                ::WaitForSingleObject(hProcess, INFINITE);

                DWORD exit_code = 0;
                ::GetExitCodeProcess(hProcess, &exit_code);

                if (throw_on_bad_exit && exit_code != 0)
                {
                    RE_THROW ProcessRunException("{} failed: exit_code={}", program_name, exit_code);
                }

                finish();
                return (int)exit_code;
            }
            else
            {
                RE_THROW ProcessRunException("{} failed to start: CreateProcessW failed", program_name);
            }
        }
        catch (const ProcessRunException &ex)
        {
            finish();
            throw ex;
        }
        catch (const std::exception &ex)
        {
            finish();
            throw ex;
        }
    }
#else
    int RunProcessOrThrow(std::string_view program_name, const fs::path &path, std::vector<std::string> cmdline,
                          bool output, bool throw_on_bad_exit, std::optional<fs::path> working_directory)
    {

        auto working_dir = working_directory->u8string();

        reproc::options options;
        options.redirect.parent = output;
        options.working_directory =
            working_directory ? (decltype(options.working_directory))working_dir.c_str() : nullptr;

        if (!path.empty())
            cmdline.insert(cmdline.begin(), path.u8string());

        reproc::process process;
        auto start_ec = process.start(cmdline, options);
        if (start_ec)
        {
            RE_THROW ProcessRunException("{} failed to start: {} (ec={})", program_name, start_ec.message(),
                                         start_ec.value());
        }

        auto [exit_code, end_ec] = process.wait(reproc::infinite);

        // process.read(reproc::stream::out, );

        if (end_ec)
        {
            RE_THROW ProcessRunException("{} failed to run: {} (ec={} exit_code={})", program_name, end_ec.message(),
                                         end_ec.value(), exit_code);
        }

        if (throw_on_bad_exit && exit_code != 0)
        {
            RE_THROW ProcessRunException("{} failed: exit_code={}", program_name, exit_code);
        }

        return exit_code;
    }
#endif

} // namespace re
