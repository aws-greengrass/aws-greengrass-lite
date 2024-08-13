import getopt
import yaml
import json
import os
import sys

global isRoot


def lower_keys(d):
    if isinstance(d, dict):
        return {k.lower(): lower_keys(v) for k, v in d.items()}
    elif isinstance(d, list):
        return [lower_keys(i) for i in d]
    else:
        return d


def dependencyParser(unit_content, dependencies):
    for dependency in dependencies:
        dependencyElement = dependencies.get(dependency, {})
        if str(dependencyElement.get("DependencyType")).lower() == "hard":
            unit_content += "After=" + dependency + ".service\n"
        else:
            unit_content += "Wants=" + dependency + ".service\n"
    ## TODO: deal with version, look conflictsWith


def fillUnitSection(yaml_data):
    unit_content = "[Unit]\n"
    unit_content += "Description=" + yaml_data["componentdescription"] + "\n"

    dependencies = yaml_data["componentdependencies"]

    if dependencies:
        dependencyParser(unit_content, dependencies)

    unit_content += "\n"

    return unit_content


def create_the_script_json_file(lifecycle, componentName):
    with open(("./%s.json" % (componentName)), "w") as f:
        json.dump(lifecycle, f)

    print(componentName + ".json file generated successfully.")


def fetch_script_section(lifecycle):
    global isRoot
    runSection = lifecycle.get("run", "")
    execCommand = ""

    if isinstance(runSection, str):
        execCommand = runSection
    else:
        scriptSection = runSection.get("Script", "")
        execCommand = scriptSection
        if runSection.get("requiresprivilege", ""):
            isRoot = True
    return execCommand


def fillServiceSection(yaml_data):
    global isRoot
    unit_content = "[Service]\n"
    unit_content += "Type=simple\n"

    # | "%t"	| Runtime directory root  |	This is either /run/ (for the system manager) or the path "$XDG_RUNTIME_DIR" resolves to (for user managers).
    unit_content += "WorkingDirectory= %t/" + yaml_data["componentname"] + "\n"

    platforms = yaml_data["manifests"]
    for platform in platforms:
        platformOS = platform.get("platform", {}).get("os")
        if platformOS == "linux" or platformOS == "*" or platformOS == "":
            lifecycleSection = platform.get("lifecycle", {})
            if lifecycleSection == "":
                selectionSection = platform.get("selections", {})
                if selectionSection == "":
                    raise Exception("Selection or Lifecycle must be mentioned")
                platformSelection = selectionSection[0]
                lifecycleSection = yaml_data["lifecycle"]
                lifecycleSection = lifecycleSection.get(platformSelection, {})

            create_the_script_json_file(
                lifecycleSection, yaml_data["componentname"]
            )

            if isinstance(lifecycleSection.get("run", ""), str):
                if lifecycleSection.get("run", "") == "":
                    return ""

            scriptSection = fetch_script_section(lifecycleSection)
            unit_content += "ExecStart=" + scriptSection + "\n"
            if isRoot:
                unit_content += "User=root\n"
                unit_content += "Group=root\n"

    return unit_content


def fillInstallSection(yaml_data):
    unit_content = "\n[Install]\n"
    unit_content += "WantedBy=GreengrassCore.target\n"
    return unit_content


def generate_systemd_unit(yaml_data):
    unit_content = ""

    unit_content += fillUnitSection(yaml_data)
    serviceSection = fillServiceSection(yaml_data)

    if serviceSection == "":
        return ""

    unit_content += serviceSection
    unit_content += fillInstallSection(yaml_data)

    return unit_content


def getCommandArgs():
    argumentList = sys.argv[1:]

    # Options
    options = "hr:"

    fileName = ""

    # Long options
    long_options = ["Help", "recipe-path="]

    try:
        # Parsing argument
        arguments, values = getopt.getopt(argumentList, options, long_options)

        # checking each argument
        for currentArgument, currentValue in arguments:
            if currentArgument in ("-h", "--Help"):
                print("Displaying Help")
                print(
                    "-r / ----recipe-path <path to a recipe> | Provide path to the recipe file that needs to be processed"
                )

            elif currentArgument in ("-r", "--recipe-path"):
                fileName = currentValue

    except getopt.error as err:
        # output error, and return with an error code
        print(str(err))

    return fileName


def main():
    global isRoot
    isRoot = False

    filePath = getCommandArgs()
    unitComponentName = ""

    with open(filePath, "r") as f:
        load_data = yaml.safe_load(f)

    # yaml_data = CaseInsensitiveDict(load_data)
    yaml_data = lower_keys(load_data)
    systemd_unit = generate_systemd_unit(yaml_data)
    unitComponentName = yaml_data["componentname"]

    if systemd_unit != "":
        with open(("./ggl%s.service" % unitComponentName), "w") as f:
            f.write(systemd_unit)
        print(
            (
                "Systemd's ggl.%s.service unit file generated successfully."
                % unitComponentName
            )
        )
    else:
        print(
            "Skiped Generating unit file as, No Linux platform's run section found in the YAML data."
        )


if __name__ == "__main__":
    main()
