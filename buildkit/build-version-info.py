from subprocess import check_output
import os

tag = check_output(['git', 'describe', '--tags']).decode('utf-8').strip()
revision = check_output(['git', 'rev-parse', 'HEAD']).decode('utf-8').strip()

version_info = tag + '-' + revision
version_info_file = '.re-last-version-info'

if os.path.exists(version_info_file) and open(version_info_file).read() == version_info:
    print(' * Re version info up to date')

    # Nothing to do
    exit()

print(' * Updating Re version info')

with open('re/version_impl.h', 'w') as header:
    header.write('#pragma once\n')
    header.write('namespace re\n')
    header.write('{\n')
    header.write('    constexpr auto kBuildVersionTag = "{tag}";\n'.format(tag=tag))
    header.write('    constexpr auto kBuildRevision = "{revision}";\n'.format(revision=revision))
    header.write('}\n')

with open(version_info_file, 'w') as info:
    info.write(version_info)
