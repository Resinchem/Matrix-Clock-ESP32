### Home Assistant Example YAML

Home Assistant is **NOT** required for full use and functionality of the matrix.  This just shows how the API commands can _optionally_ be used to add control to Home Assistant.

![Image](https://github.com/user-attachments/assets/8bf788c1-0db7-4e61-80b8-d2d55ef1002e)

The files in this folder show how I created the above dashboard and used the API to add control of some of the features of the matrix in Home Assistant.  Before you attempt to use the code, you should understand the current limitations:

- You must provide the IP address of the primary controller.  Because I'm using multiple controllers with multiple matrices, I created an input_select (dropdown) to allow me to toggle which controller I'm controlling by selecting its IP address.  If you only have one matrix/primary controller, you don't need this dropdown.  Instead, you can either hardcode the IP address in the REST commands or create an input_text helper and set its initial value to the IP address.

- Unlike the original MQTT version, the API communication is **ONE-WAY ONLY** (the API does return an immediate HTTP response code by not any current values).  While this means changing an option via dashboard WILL change the value on the matrix, change made outside of Home Assistant _will not update_ the values shown on the dashboard.  All the dashboard features will continue to work, but what is shown on the dashboard may fall "out of sync" with the actual matrix values when changes are made outside of Home Assistant, like via the web app or physical buttons.

- While I'm providing the YAML, note that nearly everything in the entities.yaml file (except the rest_commands) can be created using the UI editor.  If you are not familiar with YAML at all, this video will show you how to take the YAML similar to the that provided here and recreate it using the UI editors:

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;[Home Assistant 101: Recreate a YAML automation using the UI Automation Editor](https://youtu.be/F3YjWCs7Czc)

#### File Descriptions

_entities.yaml_

This file contains all the YAML to create the necessary helpers, REST commands, automations and script for adding the matrix to Home Assistant (and the resulting dashboard).  It is presented here as a single "package".  If you are not using packages in Home Assistant, then each integration/section needs to be split out of this file and placed in its own section either in your configuration.yaml file (or individual files if using a split configuration).

For example, to move the REST command to your main configuration.yaml file (since REST commands cannot currently be created via the UI editor), add a `rest_command:` section to your file and then copy/paste all the REST commands under this section:
```rest_command:
rest_command:
  matrix32_off:
    url: 'http://{{states("input_select.matrix32_ip_address")}}/api?system=off'
  matrix32_on:
    url: 'http://{{states("input_select.matrix32_ip_address")}}/api?system=on'
  ... etc. 
  ```
You can use this same technique for other other entity types.  If a section, such as 'input_number:' already exists, just copy the new input_number entities under the existing section, otherwise create a new section.

_dashboard.yaml_

This file contains the raw YAML used to create the dashboard shown above.  The dashboard uses conditional cards to change the controls shown based on the selected display mode (e.g. different controls shown when scoreboard is selected vs. the countdown timer).

The dashboard does use two custom controls downloaded and installed from the Home Assistant Community Store:

[Custom Button Card](https://github.com/custom-cards/button-card)

[Text Divider Row](https://github.com/iantrich/text-divider-row) (v1.4.0)*

_The current version of the text divider row (1.4.1) has an issue with the transparent background, but the author has been unresponsive and the repo hasn't been updated in five years, so you may want to consider replacing this with something like a markdown or heading card._

It is unlikely that you will create a dashboard using raw YAML like this, but it is provided as an example and show you can see things like how the custom button cards are used to launch a REST command or automation, how the conditional cards are setup, etc.

**THESE FILES ARE PROVIDED AS EXAMPLES ONLY AND WILL LIKELY REQUIRE MODIFICATION FOR YOUR OWN HOME ASSISTANT!**

Like most things in Home Assistant, there are multiple ways to accomplish the same task or goal.  What I provide here is just one technique to get you started.  Please do not request modified versions of these files.