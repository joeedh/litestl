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

export function termPrint(...args: unknown[]): string {
  const s = args.join(' ')

  const re1a = /\u001b\[[1-9][0-9]?m/
  const re1b = /\u001b\[[1-9][0-9];[0-9][0-9]?;[0-9]+m/
  const re2 = /\u001b\[39m/

  const endtag = '\u001b[39m'

  interface Token {
    type: string
    value: string
  }

  function tok(s2: string, type: string): Token {
    return {
      type: type,
      value: s2,
    }
  }

  const tokdef: [RegExp, string][] = [
    [re1a, 'start'],
    [re1b, 'start'],
    [re2, 'end'],
  ]

  let s2 = s

  const i = 0
  const tokens: Token[] = []

  while (s2.length > 0) {
    let ok = false

    let mintk: [RegExp, string] | undefined = undefined
    let mini: number | undefined = undefined
    let minslice: string | undefined = undefined
    let mintype: string | undefined = undefined

    for (const tk of tokdef) {
      const idx = s2.search(tk[0])

      if (idx >= 0 && (mini === undefined || idx < mini)) {
        minslice = s2.slice(idx, s2.length).match(tk[0])![0]
        mini = idx
        mintype = tk[1]
        mintk = tk
        ok = true
      }
    }

    if (!ok) {
      break
    }

    if (mini! > 0) {
      const chunk = s2.slice(0, mini)
      tokens.push(tok(chunk, 'chunk'))
    }

    s2 = s2.slice(mini! + minslice!.length, s2.length)
    const t = tok(minslice!, mintype!)

    tokens.push(t)
  }

  if (s2.length > 0) {
    tokens.push(tok(s2, 'chunk'))
  }

  const stack: (string | undefined)[] = []
  let cur: string | undefined

  let out = ''

  for (const t of tokens) {
    if (t.type === 'chunk') {
      out += t.value
    } else if (t.type === 'start') {
      stack.push(cur)
      cur = t.value

      out += t.value
    } else if (t.type === 'end') {
      cur = stack.pop()
      if (cur) {
        out += cur
      } else {
        out += endtag
      }
    }
  }

  return out
}
