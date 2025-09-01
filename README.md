Have you ever heard about browser hooking? Have you ever wondered how attackers could exploit login pages or payment forms to capture sensitive interactions?Have you ever wondered how attackers could exploit login pages or payment forms to capture sensitive interactions? LogMyBrowser is a personal research project and tool that allows monitoring of browser activity on any page by specifying keywords, not just login forms. While LogMyBrowser is not browser hooking at its core, it demonstrates a related concept: keystroke hooking associated with browser windows.

At a high level, it works by hooking keystrokes at the operating system level and associating them with active browser windows matching your defined keywords. During controlled testing, it was not detected by standard antivirus solutions.

!!! Tested against Sophos EDR and went undetected with 0 alerts !!!

This tool allows security professionals and red teams to safely observe user interactions with sensitive pages in a controlled lab environment, uncovering potential vulnerabilities in session handling, credential storage, or input validation. Insights gained from LogMyBrowser can help organizations proactively harden their systems against real-world attacks, demonstrating the value of proactive security testing.

In my demo i included login pages, but you can adapt to payments pages, specific URL targets , etc.

To make it easier to see the tool in action, a demo [video](./LogMyBrowser-Demo.mp4) has been included in this repository showing how LogMyBrowser monitors browser interactions based on keyword triggers.

⚠️ Important: This tool is intended strictly for ethical security research, training, and testing. Use in unauthorized environments or for malicious purposes is strictly prohibited.
