export enum termColorMap {
  black = 30,
  red = 31,
  green = 32,
  yellow = 33,
  blue = 34,
  magenta = 35,
  cyan = 36,
  teal = 36,
  white = 37,
  reset = 0,
  grey = 2,
  gray = 2,
  orange = 202,
  pink = 198,
  brown = 314,
  lightred = 91,
  peach = 210,
}

export function termColor(str: string, c: keyof typeof termColorMap | number, d: number = 0): string {
  let isTTY = true
  if (typeof globalThis.process?.stdout?.isTTY === 'boolean') {
    isTTY = process.stdout.isTTY
  }
  if (!isTTY) {
    return str
  }

  let code: number
  if (typeof c === 'string' && c in termColorMap) {
    code = termColorMap[c as keyof typeof termColorMap]
  } else {
    code = c as number
  }

  if (code > 107) {
    const s2 = '\u001b[38;5;' + code + 'm'
    return s2 + str + '\u001b[39m'
  }

  return '\u001b[' + code + 'm' + str + '\u001b[39m'
}
