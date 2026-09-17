try:
    import uos
    uos.mkdir("/usr")
except OSError:
    pass

try:
    import app_fota
    app_fota.new().update()
except Exception:
    pass
