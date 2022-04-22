colors = ['#000000', '#cd3131', '#0dbc60', '#e5e510', '#2472c8', '#bc3fbc', '#11a8cd', '#e5e5e5',
          '#666666', '#f14c4c', '#23d172', '#f5f543', '#3b8eea', '#d670d6', '#29b8db', '#ffffff']

names = ["ansiBlack", "ansiRed", "ansiGreen", "ansiYellow", "ansiBlue", "ansiMagenta", "ansiCyan",
         "ansiWhite", "ansiBrightBlack", "ansiBrightRed", "ansiBrightGreen", "ansiBrightYellow",
         "ansiBrightBlue", "ansiBrightMagenta", "ansiBrightCyan", "ansiBrightWhite"]


def topNBits(c, n):
    c = max(0, c)
    c = round(c / 2**(8-n))
    c = min(c, 255 >> (8 - n))
    return c << (8 - n)


def truncateColor(r, g, b):
    return (topNBits(r, 5), topNBits(g, 6), topNBits(b, 5))


def getColor16(r, g, b):
    r, g, b = truncateColor(r, g, b)
    return (r << 8) | (g << 3) | (b >> 3)


def getColor24(r, g, b):
    return (r << 16) | (g << 8) | b


codes = [(int(c[1:3], 16), int(c[3:5], 16), int(c[5:7], 16)) for c in colors]
codes = [truncateColor(*c) for c in codes]

colorCodes16 = [getColor16(r, g, b) for r, g, b in codes]
colorCodes24 = [getColor24(r, g, b) for r, g, b in codes]

colorCodesC = [f'0x{c:04x}' for c in colorCodes16]
colorCodesHTML = [f'#{c:06x}' for c in colorCodes24]

# for n, c in zip(names, colorCodesHTML):
#     print(f'"terminal.{n}": "{c}",')

print(f'{{{", ".join(colorCodesC)}}}')

print(f'0x{getColor16(25, 25, 25):04x}')
print(f'0x{getColor16(255, 152, 0):04x}')
