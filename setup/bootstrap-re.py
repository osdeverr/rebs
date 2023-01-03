import argparse
import os
import subprocess

parser = argparse.ArgumentParser(
                    prog = 'Re Bootstrapper',
                    description = 'Installs Re from scratch',
                    epilog = 'Enjoy using Re!')

parser.add_argument('--auto',
                    action='store_true')  # on/off flag

parser.add_argument('--arch', default='')
parser.add_argument('--main_src_dir', default='./re-main')
parser.add_argument('--bootstrap_src_dir', default='re-bootstrap-source')
parser.add_argument('--repo_url', default='https://github.com/osdeverr/rebs.git')
parser.add_argument('--main_branch', default='main')
parser.add_argument('--bootstrap_branch', default='bootstrap')
parser.add_argument('--installed_prefix', default='re-bootstrap-installed')
parser.add_argument('--out_dir', default='re-latest-build')

args = parser.parse_args()
print(args)

print()
print(' * Re Build System - Bootstrapper')
print('   This will download, build and install the latest version of Re.')
print()
print(' NOTE: This script is used to build and install Re from scratch.')
print('       If your system has an official Re release, consider downloading it instead.')
print()
print(' NOTE: PLEASE make sure CMake and Ninja are available in your PATH before proceeding!')
print('       The build requires those two to be present.')
print()
print(' Press ENTER to continue bootstrapping Re or CTRL+C to quit... ', end='')

if not args.auto:
    input()

os.makedirs(args.out_dir, exist_ok=True)
final_out_dir = os.path.realpath(args.out_dir)

print(final_out_dir)

if not os.path.exists(args.bootstrap_src_dir):
    print(f' * Downloading Re bootstrap sources from {args.repo_url}@{args.bootstrap_branch}')

    command = f'git clone {args.repo_url} --recursive --branch {args.bootstrap_branch} {args.bootstrap_src_dir}'
    subprocess.run(command.split(), check=True)
else:
    print(f' * Using existing Re bootstrap sources from {args.bootstrap_src_dir}')

bootstrap_src_dir_full = os.path.realpath(args.bootstrap_src_dir)
os.chdir(args.bootstrap_src_dir)

os.makedirs('out', exist_ok=True)
os.chdir('out')

print(' * Generating CMake files')
subprocess.run('cmake .. -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo'.split(), check=True)

print(' * Building the bootstrap source')
subprocess.run('cmake --build .'.split(), check=True)

print(' * Installing the bootstrap source')
subprocess.run(f'cmake --install . --prefix {args.installed_prefix}'.split(), check=True)

os.chdir('../..')

if not os.path.exists(args.main_src_dir):
    print(f' * Downloading Re main sources from {args.repo_url}')

    command = f'git clone {args.repo_url} --recursive --branch {args.main_branch} {args.main_src_dir}'
    subprocess.run(command.split(), check=True)
else:
    print(f' * Using existing Re main sources from {args.main_src_dir}')

os.chdir(args.main_src_dir)

print(' * Setting up build parameters')

with open('re.user.yml', mode='w') as config:
    config.write(f're-dev-deploy-path: {final_out_dir}\n')
    config.write(f'vcpkg-root-path: {bootstrap_src_dir_full}/out/_deps/vcpkg-src\n')

    if not args.auto and args.arch == '':
        args.arch = input(' > Which architecture do you want to build Re for? (x86/x64/etc, leave empty for default): ')

    if args.arch != '':
        config.write(f'arch: {args.arch}\n')

print(' * Building the latest Re')

re_path = f'{bootstrap_src_dir_full}/out/{args.installed_prefix}/bin/re'

def make_executable(path):
    mode = os.stat(path).st_mode
    mode |= (mode & 0o444) >> 2    # copy R bits to X
    os.chmod(path, mode)

if os.name != 'nt':
    make_executable(f'{bootstrap_src_dir_full}/out/{args.installed_prefix}/bin/ninja')

subprocess.run([
    re_path, 'do', 'deploy'
], check=True)

print(' * Re has succesfully been built and installed to:')
print(f'     {final_out_dir}')
print()
print(' * You should set up symlinks and/or PATH entries for the directory above, or move all of its contents')
print('   somewhere else and do the same to wherever you moved it to, to start building projects with Re.')
print()
print('   Enjoy using Re!')
print()
