export interface Logger {
  error(...args: Array<{}>): void;
  trace(...args: Array<{}>): void;
  debug(...args: Array<{}>): void;
  info(...args: Array<{}>): void;
  warn(...args: Array<{}>): void;
  fatal(...args: Array<{}>): void;
}

export class NullLogger implements Logger {
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  info(...args: Array<{}>): void {
    return;
  }
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  error(...args: Array<{}>): void {
    return;
  }
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  trace(...args: Array<{}>): void {
    return;
  }
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  warn(...args: Array<{}>): void {
    return;
  }
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  fatal(...args: Array<{}>): void {
    return;
  }
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  debug(...args: Array<{}>): void {
    return;
  }
}

export let logger = new NullLogger();

export function setLogger(newLogger: Logger) {
  logger = newLogger;
}
