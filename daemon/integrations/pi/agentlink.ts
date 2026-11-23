/*
 * agentlink: forwards pi events to the agentlink daemon via `agentlink-hook pi <Event>`
 * (JSON payload on stdin), the same way Claude Code command hooks do.
 *
 * Install: agentlink hooks pi > ~/.pi/agent/extensions/agentlink.ts
 *
 * Mapping (Claude-shaped event names, pi's own tool names/args):
 *   session_start        -> SessionStart
 *   session_shutdown     -> SessionEnd (waits up to 1.5 s for the queue to drain)
 *   before_agent_start   -> UserPromptSubmit (no prompt text)
 *   tool_execution_start -> PreToolUse
 *   tool_execution_end   -> PostToolUse / PostToolUseFailure (error = last 4 KiB of text)
 *   agent_settled        -> Stop (last_assistant_message) / StopFailure (error)
 *   ui_prompt_start      -> Notification (elicitation_dialog)
 *
 * Env: AGENTLINK_HOOK overrides the shim path (default "agentlink-hook");
 *      AGENTLINK_PI=0 disables the extension.
 */
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { resolve } from 'node:path';
import type { ExtensionAPI } from '@earendil-works/pi-coding-agent';

const MAX_PENDING = 256;
const CHILD_TIMEOUT_MS = 3000;
const DRAIN_TIMEOUT_MS = 1500;

type Payload = Record<string, unknown>;
export type Line = { event: string; payload: Payload };
export type SpawnFn = (event: string, input: string) => Promise<'ok' | 'enoent'>;

export function resultText(result: any): string {
  const parts = Array.isArray(result?.content) ? result.content : [];
  return parts
    .filter((p: any) => p && p.type === 'text' && typeof p.text === 'string')
    .map((p: any) => p.text)
    .join('');
}

export type LastAssistant = { text: string; stopReason?: string; errorMessage?: string };

export function lastAssistant(messages: any): LastAssistant | undefined {
  if (!Array.isArray(messages)) return undefined;
  for (let i = messages.length - 1; i >= 0; i--) {
    const m = messages[i];
    if (m?.role !== 'assistant') continue;
    const parts = Array.isArray(m.content) ? m.content : [];
    const text = parts
      .filter((p: any) => p?.type === 'text' && typeof p.text === 'string')
      .map((p: any) => p.text)
      .join('\n\n')
      .trim();
    return { text, stopReason: m.stopReason, errorMessage: m.errorMessage };
  }
  return undefined;
}

export function stopLine(last: LastAssistant | undefined): Line {
  if (last?.stopReason === 'error') {
    return { event: 'StopFailure', payload: { error: (last.errorMessage ?? '').slice(0, 1024) } };
  }
  const payload: Payload = {};
  if (last && last.stopReason !== 'aborted' && last.text) {
    payload.last_assistant_message = last.text.slice(0, 16384);
  }
  return { event: 'Stop', payload };
}

export function failureText(result: any): string {
  return resultText(result).slice(-4096);
}

export function endLine(
  toolName: string,
  toolInput: any,
  isError: boolean,
  result: any,
  created: boolean,
): Line {
  const payload: Payload = { tool_name: toolName, tool_input: toolInput };
  if (isError) {
    payload.error = failureText(result);
    return { event: 'PostToolUseFailure', payload };
  }
  if (toolName === 'write' && created) payload.tool_response = { type: 'create' };
  return { event: 'PostToolUse', payload };
}

export function createSender(spawnFn: SpawnFn) {
  let pending = 0;
  let disabled = false;
  let tail: Promise<void> = Promise.resolve();
  return {
    send(event: string, payload: Payload) {
      if (disabled || pending >= MAX_PENDING) return;
      let input: string;
      try {
        input = JSON.stringify(payload);
      } catch {
        return;
      }
      pending++;
      tail = tail.then(async () => {
        try {
          if (!disabled && (await spawnFn(event, input)) === 'enoent') disabled = true;
        } catch {
          // never break pi
        } finally {
          pending--;
        }
      });
    },
    drain(timeoutMs: number): Promise<void> {
      return new Promise((done) => {
        const t = setTimeout(done, timeoutMs);
        tail.then(() => {
          clearTimeout(t);
          done();
        });
      });
    },
  };
}

function realSpawn(event: string, input: string): Promise<'ok' | 'enoent'> {
  return new Promise((done) => {
    let settled = false;
    let timer: ReturnType<typeof setTimeout> | undefined;
    const finish = (r: 'ok' | 'enoent') => {
      if (settled) return;
      settled = true;
      if (timer) clearTimeout(timer);
      done(r);
    };
    try {
      const child = spawn(process.env.AGENTLINK_HOOK || 'agentlink-hook', ['pi', event], {
        stdio: ['pipe', 'ignore', 'ignore'],
      });
      timer = setTimeout(() => {
        try {
          child.kill('SIGKILL');
        } catch {}
        finish('ok');
      }, CHILD_TIMEOUT_MS);
      child.on('error', (e: any) => finish(e?.code === 'ENOENT' ? 'enoent' : 'ok'));
      child.on('exit', () => finish('ok'));
      child.stdin?.on('error', () => {});
      child.stdin?.end(input);
    } catch {
      finish('ok');
    }
  });
}

export default function (pi: ExtensionAPI) {
  if (process.env.AGENTLINK_PI === '0') return;
  const sender = createSender(realSpawn);
  const calls = new Map<string, { args: any; created: boolean }>();
  let last: LastAssistant | undefined;

  const emit = (ctx: any, event: string, extra: Payload = {}) => {
    try {
      const session_id = ctx?.sessionManager?.getSessionId?.();
      if (!session_id) return;
      sender.send(event, { session_id, cwd: ctx.cwd, ...extra });
    } catch {
      // skip
    }
  };

  pi.on('session_start', (_e, ctx) => emit(ctx, 'SessionStart'));
  pi.on('session_shutdown', async (_e, ctx) => {
    emit(ctx, 'SessionEnd');
    await sender.drain(DRAIN_TIMEOUT_MS);
  });
  pi.on('before_agent_start', (_e, ctx) => emit(ctx, 'UserPromptSubmit'));
  pi.on('tool_execution_start', (e, ctx) => {
    try {
      let created = false;
      if (e.toolName === 'write' && typeof e.args?.path === 'string') {
        created = !existsSync(resolve(ctx.cwd, e.args.path));
      }
      calls.set(e.toolCallId, { args: e.args, created });
      emit(ctx, 'PreToolUse', { tool_name: e.toolName, tool_input: e.args });
    } catch {
      // skip
    }
  });
  pi.on('tool_execution_end', (e, ctx) => {
    try {
      const c = calls.get(e.toolCallId);
      calls.delete(e.toolCallId);
      const line = endLine(e.toolName, c?.args, e.isError, e.result, c?.created ?? false);
      emit(ctx, line.event, line.payload);
    } catch {
      // skip
    }
  });
  pi.on('agent_end', (e) => {
    try {
      last = lastAssistant(e.messages);
    } catch {
      last = undefined;
    }
  });
  pi.on('agent_settled', (_e, ctx) => {
    const line = stopLine(last);
    last = undefined;
    emit(ctx, line.event, line.payload);
  });
  pi.on('ui_prompt_start', (_e, ctx) =>
    emit(ctx, 'Notification', { notification_type: 'elicitation_dialog' }),
  );
}
