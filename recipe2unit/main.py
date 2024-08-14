import getopt
import yaml
import json
import os
import sys

global isRoot

global recipe_runner_path


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
    try:
        with open(("./%s.json" % (componentName)), "w") as f:
            json.dump(lifecycle, f)
        print(componentName + ".json file generated successfully.")

    except Exception as error:
        print(str(error))


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


def create_the_bash_script_run_file(script_section, filename):
    try:
        with open(filename, "w") as f:
            f.write(script_section)
        print(filename + " script file generated successfully.")
    except Exception as error:
        print(str(error))


def fillServiceSection(yaml_data):
    global isRoot
    global recipe_runner_path

    unit_content = "[Service]\n"
    unit_content += "Type=simple\n"

    # | "%t"	| Runtime directory root  |	This is either /run/ (for the system manager) or the path "$XDG_RUNTIME_DIR" resolves to (for user managers).
    unit_content += "WorkingDirectory= %t/" + yaml_data["componentname"] + "\n"
    bash_script_file_name = (
        "ggl." + yaml_data["componentname"] + ".RunBashScript"
    )

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

            if lifecycleSection.get("run", "") == "":
                return ""

            create_the_bash_script_run_file(
                fetch_script_section(lifecycleSection), bash_script_file_name
            )

            unit_content += (
                "ExecStart="
                + recipe_runner_path
                + " -p "
                + os.getcwd()
                + "/"
                + bash_script_file_name
                + "\n"
            )
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
    recipeRunnerPath = ""

    # Long options
    long_options = ["Help", "recipe-path=", "recipe-runner-path="]

    try:
        # Parsing argument
        arguments, values = getopt.getopt(argumentList, options, long_options)

        # checking each argument
        for currentArgument, currentValue in arguments:
            if currentArgument in ("-h", "--Help"):
                print("Displaying Help")
                print(
                    "-r / --recipe-path <path to a recipe> | Provide path to the recipe file that needs to be processed"
                )
                print(
                    "-r / --recipe-runner-path <path to a recipe> | Provide absolute path to the recipe runner binary"
                )
            elif currentArgument in ("-r", "--recipe-path"):
                fileName = currentValue
            elif currentArgument in ("-e", "--recipe-runner-path"):
                recipeRunnerPath = currentValue

    except getopt.error as err:
        # output error, and return with an error code
        print(str(err))

    return (fileName, recipeRunnerPath)


def main():
    global isRoot
    global recipe_runner_path

    isRoot = False

    file_path, recipe_runner_path = getCommandArgs()

    if file_path == "" or recipe_runner_path == "":
        print("Error: file path or  recipe runner path not provided")

    unitComponentName = ""

    with open(file_path, "r") as f:
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
