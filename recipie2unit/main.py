import yaml
from collections import UserDict
import os

global isRoot

class CaseInsensitiveDict(UserDict):
    def __init__(self, data=None):
        super().__init__()
        if data:
            for key, value in data.items():
                self.__setitem__(key, value)

    def __getitem__(self, key):
        key = key.lower()
        return self.data[key]

    def __setitem__(self, key, value):
        key = key.lower()
        self.data[key] = value

    def __delitem__(self, key):
        key = key.lower()
        del self.data[key]

def dependencyParser(unit_content, dependencies):
    for dependency in dependencies:
        dependencyElement = dependencies.get(dependency,{})
        if str(dependencyElement.get("DependencyType")).lower() == "hard":
            unit_content += "After=" + dependency + ".service\n"
        else:
            unit_content += "Wants=" + dependency +".service\n"
    ## TODO: deal with version, look conflictsWith                                                                


def fillUnitSection(yaml_data):
    unit_content = "[Unit]\n"
    unit_content += "Description=" + yaml_data["ComponentDescription"]+ "\n"
    
    dependencies = yaml_data["ComponentDependencies"]
    
    if (dependencies):
        dependencyParser(unit_content, dependencies)
    
    unit_content += "\n"
    
    return unit_content
    
def fetch_script_section(lifecycle):
    global isRoot
    runSection = lifecycle.get("run", "")
    execCommand = ""
    
    if isinstance(runSection, str):
        execCommand = runSection    
    else:
        scriptSection  = runSection.get("Script","")
        execCommand = scriptSection
        if (runSection.get("RequiresPrivilege","")):
            isRoot = True
    return execCommand

def fillServiceSection(yaml_data):
    global isRoot
    unit_content = "[Service]\n"
    unit_content += "Type=simple\n"
    
    # | "%t"	| Runtime directory root  |	This is either /run/ (for the system manager) or the path "$XDG_RUNTIME_DIR" resolves to (for user managers).
    unit_content += "WorkingDirectory= %t/" + yaml_data["ComponentName"]+ "\n"
    
    platforms = yaml_data["Manifests"]
    for platform in platforms:
        platformOS = platform.get("Platform", {}).get("os")
        if platformOS == "linux" or platformOS == "*" or platformOS == "":
            lifecycleSection = platform.get("Lifecycle", {})
            if (lifecycleSection == ""):
                selectionSection = platform.get("Selections", {})
                if (selectionSection == ""):
                   raise Exception("Selection or Lifecycle must be mentioned")
                platformSelection = selectionSection[0]
                lifecycleSection =  yaml_data["Lifecycle"]
                lifecycleSection = lifecycleSection.get(platformSelection, {})
                
            scriptSection =  fetch_script_section(lifecycleSection)
            unit_content += "ExecStart=" + scriptSection + "\n"
            if (isRoot):
                unit_content +="User=root\n"
                unit_content +="Group=root\n"
            
    return unit_content
    
def fillInstallSection(yaml_data):
    unit_content = "\n[Install]\n"
    unit_content += "WantedBy=GreengrassCore.target\n"
    return unit_content
    

def generate_systemd_unit(yaml_data):  
    unit_content = ""
    
    unit_content += fillUnitSection(yaml_data)
    unit_content += fillServiceSection(yaml_data)
    unit_content += fillInstallSection(yaml_data) 

    return unit_content

def main():
    current_dir = os.getcwd()
    global isRoot
    isRoot =False
    path = current_dir + "/recipe-parser/recipe.yml"
    with open( path, "r") as f:
        load_data = yaml.safe_load(f)

    yaml_data = CaseInsensitiveDict(load_data)
    systemd_unit = generate_systemd_unit(yaml_data)
    if systemd_unit:
        with open("./hello_world.service", "w") as f:
            f.write(systemd_unit)
        print("Systemd unit file generated successfully.")
    else:
        print("No Linux platform found in the YAML data.")
    

if __name__=="__main__": 
    main()