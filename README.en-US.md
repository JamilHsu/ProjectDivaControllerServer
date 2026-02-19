This application can transform your Android tablet or smartphone into a controller for *Hatsune Miku: Project Diva*, providing a touch-based control experience similar to the Nintendo Switch version's Tap Play.  
This application must be used in conjunction with [ProjectDivaControllerClient](https://github.com/JamilHsu/ProjectDivaControllerClient), which runs on the Android device.  
Version that works on iOS -> [ProjectDivaController](https://github.com/JamilHsu/ProjectDivaController)

![image](https://raw.githubusercontent.com/JamilHsu/ProjectDivaControllerServer/refs/heads/master/ProjectDivaController%E9%81%8B%E4%BD%9C%E7%95%AB%E9%9D%A2.jpg)

---

## Connection Setup

After launching the program, it will automatically enumerate all IP addresses available on the host computer. Enter the appropriate IP address on the tablet or smartphone to establish a connection.

During the first launch, antivirus software may issue warnings, as this program is capable of receiving network commands and simulating keyboard input via [`SendInput`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput). Additionally, the system firewall may prompt you to allow network access.

You may connect to the computer using **any method**, as long as the Android device can reach the computer’s **IPv4 address**.  
For example, the following methods all work:

* USB tethering  
* Mobile hotspot  
* PC hotspot   
* `adb reverse tcp:3939 tcp:3939`

Please select the IP address that corresponds to the network interface actually used for the connection.  
Multiple IP addresses may be reachable at the same time; however, the actual network routing path and latency characteristics may differ depending on which IP is selected. Be careful not to unintentionally use a wireless route when a wired (USB) route is available.

---

## Runtime Behavior

After a successful connection, the console window on the PC will begin displaying touch input received from the tablet and the corresponding keyboard events generated on the computer.

At this point, the program should be operational. However, one final configuration step is required.

---

## Key Mapping Configuration

Open the file:

`ProjectDivaControllerSettings.txt`

Edit the mappings between the in-game buttons (△ □ × ◯) and the corresponding keyboard keys (e.g., W S A D).  
After saving the file, restart the program for the changes to take effect.

---

## Other Details

Although the program technically simulates keyboard input, its control logic is designed to resemble a PS4 controller. For example:

* In the rightmost judgment area:
  * The first finger is mapped to ◯  
  * The second finger is mapped to 🡆  
  * Additional fingers are ignored

Once a touch is registered, the corresponding button will remain pressed regardless of finger movement. Even if the finger leaves the judgment area, the button will not be released until the touch ends.

---

## Slide Detection Logic

The slide detection mechanism operates as follows:

* The two fingers with the highest horizontal velocities are evaluated.  
* If their horizontal speed exceeds a predefined activation threshold, a left or right analog stick input is assigned based on their positions.  
* As long as the horizontal speed remains above 1/8 of the activation threshold, the stick input is continuously held.

The activation threshold is based on physical distance. Devices with wider screens will have proportionally higher default thresholds.  
You may manually adjust this behavior by changing `slide_require_multiplier` in `ProjectDivaControllerSettings.txt` to a value other than `1.0`.

Slide detection and tap detection are evaluated independently. This means that a sliding touch will also trigger a regular button press. This behavior does not affect gameplay, as Slide Icons never occur simultaneously with Melody Icons. Additionally, sliding inputs can still be performed using the same finger while holding a Hold Target.

A maximum of **two simultaneous sliding inputs** is supported. As a result, simultaneous left and right Slide Icons should not be performed with three or more fingers.  
For example, if four fingers simultaneously input ⇀⇀↼↼, the resulting input may become ⇀⇀, ↼↼, or ⇀↼ unpredictably.  
**The entire screen, including the four-color button area, can perform sliding operation, not just the yellow slider area at the top.** The only difference in the yellow slider area at the top is that clicking it won't cause a regular button to be pressed, and it's multiplied by 16 when calculating horizontal speed. If you want, you can set `slide_require_multiplier` to a larger value, making it less likely for the regular button area to accidentally cause a slide.

---

## Debug Output and Latency

After verifying normal operation, you may set the following options to `0` in the configuration file:

* `output_received_message`  
* `output_keyboard_operation`

Disabling these outputs can slightly reduce latency.

The displayed **Round-Trip Time (RTT)** value is provided for reference only and is highly inaccurate. It cannot be used to estimate one-way latency by simply dividing it by two. The only guarantee is that the actual latency will never exceed this RTT value. The RTT is currently derived from the ping process used solely for connection verification.

The current latency measurement system evaluates **relative statistical variations between multiple delays**, rather than true one-way latency. Since precise one-way latency cannot be accurately measured in the current implementation, only relative fluctuations are analyzed.  
You may set `test_connection_stability` to `0` to disable the latency measurement (and the MessageBox it produces).
