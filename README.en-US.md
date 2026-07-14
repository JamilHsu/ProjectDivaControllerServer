This application can transform your Android tablet or smartphone into a controller for *Hatsune Miku: Project Diva*, providing a touch-based control experience similar to the Nintendo Switch version's Tap Play.  
This application must be used in conjunction with [ProjectDivaControllerClient](https://github.com/JamilHsu/ProjectDivaControllerClient), which runs on the Android device.  
Version that works on iOS -> [ProjectDivaController](https://github.com/JamilHsu/ProjectDivaController)

![image](https://raw.githubusercontent.com/JamilHsu/ProjectDivaControllerServer/refs/heads/master/ProjectDivaController%E9%81%8B%E4%BD%9C%E7%95%AB%E9%9D%A2.jpg)

---

## Connection Setup

After launching the program, it will automatically enumerate all IP addresses available on the host computer. Enter the appropriate IP address on the Android client to establish a connection.

During the first launch, antivirus software may issue warnings, as this program is capable of receiving network commands and simulating keyboard input via [`SendInput`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput). Additionally, the system firewall may prompt you to allow network access.

You may connect to the computer using **any method**, as long as the Android device can reach the computer's **IPv4 address**.  
For example, the following methods all work:

* USB tethering  
* Mobile hotspot  
* PC hotspot   
* `adb reverse tcp:3939 tcp:3939`

Please select the IP address that corresponds to the network interface actually used for the connection.  
Multiple IP addresses may be reachable at the same time; however, the actual network routing path and latency characteristics may differ depending on which IP is selected. Be careful not to unintentionally use a wireless route when a wired (USB) route is available.


At this point, the program should be operational. However, one final configuration step is required.

---

## Key Mapping Configuration

(No necessary if the MM+ keyboard configuration uses the default settings.)  

Open the file:  
`ProjectDivaControllerSettings.txt`

Edit the mappings between the in-game buttons (△ □ × ◯) and the corresponding keyboard keys (e.g., W S A D).  
After saving the file, restart the program for the changes to take effect.

## Other detail

The included DLL and `config.toml` are a tiny helper mod. Its sole purpose is to automatically launch the EXE and terminate it after the game closes. This allows this application to be managed by a mod manager just like a regular mod. The helper mod also enables the application to work on Linux without any special configuration. If you are not using mods, you can safely delete the DLL and `config.toml`.