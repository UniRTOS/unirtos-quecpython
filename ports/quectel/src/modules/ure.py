class _Pattern:
    def __init__(self, pattern):
        self.pattern = pattern

    def match(self, string):
        return match(self.pattern, string)

    def search(self, string):
        return search(self.pattern, string)

    def split(self, string, max_split=-1):
        return string.split(self.pattern, max_split)


def compile(pattern, flags=0):
    return _Pattern(pattern)


def match(pattern, string):
    if string.startswith(pattern):
        return True
    return None


def search(pattern, string):
    if string.find(pattern) >= 0:
        return True
    return None


def split(pattern, string, max_split=-1):
    return string.split(pattern, max_split)


def sub(pattern, repl, string, count=0):
    return string.replace(pattern, repl, count if count else -1)
