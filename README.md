# Buster's ESP32 Catfeeder

It was a petlibro feeder but it failed right out of the box, so I stuck an ESP32 and a FET breakout on it. Now it works. 

## Web Interface

Available only on the LAN (thanks to mDNS) as catfeeder0.local.

### /hello

### /feed

Takes query parameter `clicks` and feeds that many clicks

```bash
catfeeder0.local/feed?clicks=2
```

### /schedule

Takes query parameters:
- **index** for index in feeding schedule list (currently 0-3)
- **hr** for hour 0-23
- **min** for minute 0-59
- **clicks** for meal size in mechanism clicks

```bash
catfeeder0.local/schedule?index=0&hr=6&min=0&clicks=3
```