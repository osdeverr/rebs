from subprocess import check_output

print(' * Updating Re version info')

tag = check_output(['git', 'describe', '--tags']).decode('utf-8').strip()
revision = check_output(['git', 'rev-parse', 'HEAD']).decode('utf-8').strip()

with open('re/version_impl.h', 'w') as header:
    header.write('#pragma once\n')
    header.write('namespace re\n')
    header.write('{\n')
    header.write('    constexpr auto kBuildVersionTag = "{tag}";\n'.format(tag=tag))
    header.write('    constexpr auto kBuildRevision = "{revision}";\n'.format(revision=revision))
    header.write('}\n')
