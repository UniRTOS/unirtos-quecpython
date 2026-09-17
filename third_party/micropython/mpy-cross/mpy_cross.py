class CrossCompileError(Exception):
    pass


def compile(*args, **kwargs):
    raise CrossCompileError("mpy-cross is not available in this SDK snapshot")
