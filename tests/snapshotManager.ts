import fs from 'fs'
import {createPatch} from 'diff'
import {termColor, termColorMap} from './termColor.js'

interface ISnapShot {
  name: string
  value: string
}

export class SnapShotManager {
  fileName: string
  snapShots: ISnapShot[] = []
  writeMode: boolean
  private cur = 0

  constructor(key: string, writeMode = process.argv.includes('--update-snapshots') || process.argv.includes('-u')) {
    this.writeMode = writeMode
    this.fileName = key + '__snapshots.json'
    if (fs.existsSync(this.fileName)) {
      this.snapShots = JSON.parse(fs.readFileSync(this.fileName, 'utf8'))
    }
  }

  reset() {
    this.cur = 0
  }

  snapShotDiff(snapshot: ISnapShot, name: string, value: string) {
    if (snapshot.name !== name) {
      console.log(`Missing snapshot: ${name}`)
      return
    }

    let diff = createPatch(
      snapshot.name,
      snapshot.value + '\n',
      value + '\n',
      '',
      '', //
      {
        headerOptions: {
          //
          includeIndex: false,
          includeUnderline: false,
          includeFileHeaders: false,
        },
      }
    )
    const colorDiff = (s: string) => {
      if (!process.stderr.isTTY) {
        return s
      }
      let buf = ''
      const lines = s.split('\n')
      for (let line of lines) {
        if (line.startsWith('-')) {
          line = termColor(line, 'red')
        } else if (line.startsWith('+')) {
          line = termColor(line, 'green')
        }
        buf += line + '\n'
      }
      return buf
    }
    diff = colorDiff(diff)
    process.stderr.write(`\nSnapshot ${name} differs:\n${diff}\n`)
    process.exit(-1)
  }
  private termColor(str: string, c: keyof typeof termColorMap | number, d: number = 0) {
    if (process.stdout.isTTY) {
      return termColor(str, c, d)
    }
    return str
  }
  log(name: string, ...args: any[]) {
    this.expect(name, args.join(' '))
  }

  expect(name: string, value: string) {
    process.stdout.write(`\n${this.termColor(`=====${name}:=====`, 'teal')}\n`)
    const nl = value.endsWith('\n') ? '' : '\n'
    process.stdout.write(value + nl)

    let snapshot = this.snapShots[this.cur]
    let write = false

    if (snapshot === undefined) {
      snapshot = {name, value}
      this.snapShots.push(snapshot)
      write = true
    } else if (snapshot.name !== name || snapshot.value !== value) {
      if (this.writeMode) {
        snapshot = {name, value}
        write = true
      } else {
        this.snapShotDiff(snapshot, name, value)
      }
    }

    if (write) {
      this.snapShots[this.cur] = snapshot
      console.log(`writing snapshot ${this.fileName}:${name}`)
      fs.writeFileSync(this.fileName, JSON.stringify(this.snapShots, undefined, 2))
    }
    this.cur++
  }
}
