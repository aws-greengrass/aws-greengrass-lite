import getopt
import yaml
import os
import sys
from env_var import EnvironmentVariables

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


def fetch_script_section(lifecycle, phase):
    global isRoot
    runSection = lifecycle.get(phase, "")
    execCommand = ""

    if isinstance(runSection, str):
        execCommand = runSection
    else:
        scriptSection = runSection.get("script", "")
        execCommand = scriptSection
        if runSection.get("requiresprivilege", ""):
            isRoot = runSection.get("requiresprivilege", "")
    return execCommand


def create_the_bash_script_file(script_section, filename):
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
    bash_script_file_name = "ggl." + yaml_data["componentname"] + ".script."

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

            create_the_bash_script_file(
                fetch_script_section(lifecycleSection, "install"),
                bash_script_file_name + "install",
            )

            run_phase_selection = ""

            if lifecycleSection.get("startup", "") != "":
                run_phase_selection = "startup"
            else:
                if lifecycleSection.get("run", "") != "":
                    run_phase_selection = "run"
                else:
                    return ""

            create_the_bash_script_file(
                fetch_script_section(lifecycleSection, run_phase_selection),
                bash_script_file_name + run_phase_selection,
            )

            unit_content += (
                "ExecStart="
                + recipe_runner_path
                + " -p "
                + os.getcwd()
                + "/"
                + bash_script_file_name
                + run_phase_selection
                + "\n"
            )
            if run_phase_selection == "startup":
                unit_content += "RemainAfterExit=true\n"
                unit_content += "Type=oneshot\n"
            if isRoot:
                unit_content += "User=root\n"
                unit_content += "Group=root\n"

    return unit_content


def add_environment_variables(environment_var: EnvironmentVariables):
    unit_content = ""

    unit_content += (
        'Environment="AWS_IOT_THING_NAME=' + environment_var.thing_name + '"\n'
    )

    if len(environment_var.aws_region) != 0:
        unit_content += (
            'Environment="AWS_REGION=' + environment_var.aws_region + '"\n'
        )
        unit_content += (
            'Environment="AWS_DEFAULT_REGION='
            + environment_var.aws_region
            + '"\n'
        )

    if len(environment_var.ggc_version) != 0:
        unit_content += (
            'Environment="GGC_VERSION=' + environment_var.ggc_version + '"\n'
        )

    if len(environment_var.gg_root_ca_path) != 0:
        unit_content += (
            'Environment="GG_ROOT_CA_PATH='
            + environment_var.gg_root_ca_path
            + '"\n'
        )

    unit_content += (
        'Environment="AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT='
        + environment_var.socket_path
        + '"\n'
    )

    if len(environment_var.aws_container_auth_token) != 0:
        unit_content += (
            'Environment="AWS_CONTAINER_AUTHORIZATION_TOKEN='
            + environment_var.aws_container_auth_token
            + '"\n'
        )

    if len(environment_var.aws_container_cred_url) != 0:
        unit_content += (
            'Environment="AWS_CONTAINER_CREDENTIALS_FULL_URI='
            + environment_var.aws_container_cred_url
            + '"\n'
        )

    return unit_content


def fillInstallSection(yaml_data):
    unit_content = "\n[Install]\n"
    unit_content += "WantedBy=GreengrassCore.target\n"
    return unit_content


def generate_systemd_unit(yaml_data, environment_var: EnvironmentVariables):
    unit_content = ""

    unit_content += fillUnitSection(yaml_data)
    serviceSection = fillServiceSection(yaml_data)

    if serviceSection == "":
        return ""

    unit_content += serviceSection
    unit_content += add_environment_variables(environment_var)

    unit_content += fillInstallSection(yaml_data)

    return unit_content


def getCommandArgs():
    argumentList = sys.argv[1:]
    env_var = EnvironmentVariables()

    # Options
    options = "hr:e:s:t:g:n:o:a:u:"

    fileName = ""
    recipeRunnerPath = ""

    # Long options
    long_options = [
        "Help",
        "recipe-path=",
        "recipe-runner-path=",
        "socket-path=",
        "thing_name=",
        "aws-region=",
        "ggc-version=",
        "rootca-path=",
        "auth-token=",
        "cred-url=",
    ]

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
            elif currentArgument in ("-s", "--socket-path"):
                env_var.socket_path = currentValue
            elif currentArgument in ("-t", "--thing-name"):
                env_var.thing_name = currentValue

            elif currentArgument in ("-g", "--aws-region"):
                env_var.aws_region = currentValue

            elif currentArgument in ("-n", "--ggc-version"):
                env_var.ggc_version = currentValue

            elif currentArgument in ("-o", "--rootca-path"):
                env_var.gg_root_ca_path = currentValue

            elif currentArgument in ("-a", "--auth-token"):
                env_var.aws_container_auth_token = currentValue

            elif currentArgument in ("-u", "--cred-url"):
                env_var.aws_container_auth_token = currentValue

    except getopt.error as err:
        # output error, and return with an error code
        print(str(err))

    return (fileName, recipeRunnerPath, env_var)


def main():
    global isRoot
    global recipe_runner_path

    isRoot = False

    file_path, recipe_runner_path, environment_var = getCommandArgs()

    if (
        len(file_path) == 0
        or len(recipe_runner_path) == 0
        or len(environment_var.thing_name) == 0
        or len(environment_var.socket_path) == 0
    ):
        print("Error: Necessary parameters are not set")
        return

    unitComponentName = ""

    with open(file_path, "r") as f:
        load_data = yaml.safe_load(f)

    # yaml_data = CaseInsensitiveDict(load_data)
    yaml_data = lower_keys(load_data)
    systemd_unit = generate_systemd_unit(yaml_data, environment_var)
    unitComponentName = yaml_data["componentname"]

    if systemd_unit != "":
        with open(("./ggl.%s.service" % unitComponentName), "w") as f:
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
