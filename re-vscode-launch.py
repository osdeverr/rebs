import json
import os

workspaceFolder = os.getcwd()

meta = None
with open('.re-cache/meta/full.json', 'r') as outfile:
    meta = json.load(outfile)
if meta == None:
    print('Required call re meta before running generation launch.json')

current_launch = []
try:
    with open('.vscode/launch.json', 'r') as outfile:
        try:
            data = json.load(outfile)
            current_launch = data['configurations']
        except Exception as ex:
            print('Parse launch.json error:', ex)
            exit(1)
except Exception as ex:
    pass

def read_meta_data():
    result = []
    for index, (key, value) in enumerate(meta['targets'].items()):
        try:
            if value['type'] == 'executable':
                result.append({'main-artifact': value['main-artifact'], 'module': value['module'],
                              'arch': value['cxx']['arch'], 'config': value['cxx']['config']})
        except:
            pass
    return result


def generate_launch_name(main_artifact, module, arch, config):
    return f"(Windows) Launch -> [{arch}] [{config}] {module}"


def create_launch_cfg_item(main_artifact, module, arch, config):
    rel = os.path.relpath(main_artifact, workspaceFolder)

    return {
        "name": generate_launch_name(main_artifact, module, arch, config),
        "type": "cppvsdbg",
        "request": "launch",
        "program": "${workspaceFolder}/" + rel,
        "args": [],
        "stopAtEntry": False,
        "cwd": "${fileDirname}",
        "environment": [],
        "console": "externalTerminal",
        "re-defined": True
        # "re-latest-meta": True,
        # "re-arch": f'{arch}',
        # "re-config": f'{config}',
        # "re-module": f'{module}'
    }

meta_data = read_meta_data()

configurations = []
# for launch_config in current_launch:
#     if launch_config.get('re-defined') == True:
#         pass
#     else:
#         configurations.append(launch_config)

for launch_config in current_launch:
    if 're-defined' in launch_config:
        current_launch.remove(launch_config)

# print("current: ", current_launch)

for module_info in meta_data:
    main_artifact = module_info['main-artifact']
    module = module_info['module']
    arch = module_info['arch']
    config = module_info['config']

    found = False
    for launch_config in current_launch:
        if launch_config['name'] == generate_launch_name(main_artifact, module, arch, config):
            launch_config = create_launch_cfg_item(
                main_artifact, module, arch, config)
            found = True
            break

    if found:
        continue

    cfg = create_launch_cfg_item(main_artifact, module, arch, config)
    configurations.append(cfg)

# print("current: ", current_launch)
# print('configs: ', configurations)

configurations = current_launch + configurations

data = {'version': '0.2.0', 'configurations': configurations}

with open('.vscode/launch.json', 'w') as outfile:
    outfile.write(json.dumps(data, indent=4))

# print('.vscode/launch.json is ready')