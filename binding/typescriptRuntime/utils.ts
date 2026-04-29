export function hashString(s: string) {
  let n = 0
  let r = 1
  for (let i = 0; i < s.length; i++) {
    r = (Math.imul(r, 1664525) + 1013904223) | 0
    n = (Math.imul(n, 31) + (s.charCodeAt(i) ^ r)) | 0
  }
  return Math.abs(n)
}
