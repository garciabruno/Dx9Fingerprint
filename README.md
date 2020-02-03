# Dx9Fingerprint
DLL utility to fingerprint models (primitive draw calls) on DirectX9 based applications by either hooking DirectX9 EndScene function or Steam's Gameoverlay Present function

## Using Steam Gameoverlay

By using "Add a non-steam product" on your Steam Library you can launch **any** application and Steam will automatically hook the render pipeline to draw their overlay. We can hook Steam's gameoverlay dll to draw our own things

## Special thanks

* Phil (https://github.com/NoWayz/) for KrunkHook
* Gabriel Freitas (https://github.com/gfreivasc/) for VMTHook
