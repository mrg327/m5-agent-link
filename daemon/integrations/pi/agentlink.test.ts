import test from 'node:test';
import assert from 'node:assert/strict';
import {
  createSender,
  endLine,
  failureText,
  lastAssistant,
  resultText,
  stopLine,
} from './agentlink.ts';
import type { SpawnFn } from './agentlink.ts';

test('queue sends serially in order', async () => {
  const log: string[] = [];
  const fake: SpawnFn = async (event) => {
    log.push(`start ${event}`);
    await new Promise((r) => setTimeout(r, event === 'PreToolUse' ? 20 : 1));
    log.push(`end ${event}`);
    return 'ok';
  };
  const s = createSender(fake);
  s.send('PreToolUse', {});
  s.send('PostToolUse', {});
  await s.drain(1000);
  assert.deepEqual(log, [
    'start PreToolUse',
    'end PreToolUse',
    'start PostToolUse',
    'end PostToolUse',
  ]);
});

test('ENOENT disables further sending', async () => {
  let calls = 0;
  const s = createSender(async () => {
    calls++;
    return 'enoent';
  });
  s.send('A', {});
  s.send('B', {});
  await s.drain(1000);
  s.send('C', {});
  await s.drain(1000);
  assert.equal(calls, 1);
});

test('drain is bounded', async () => {
  const s = createSender(() => new Promise(() => {}));
  s.send('A', {});
  const t = Date.now();
  await s.drain(50);
  assert.ok(Date.now() - t < 500);
});

test('failure text keeps the tail', () => {
  const result = {
    content: [
      { type: 'text', text: 'x'.repeat(5000) },
      { type: 'text', text: '\nCommand exited with code 2' },
    ],
  };
  const t = failureText(result);
  assert.equal(t.length, 4096);
  assert.ok(t.endsWith('Command exited with code 2'));
  assert.equal(resultText({}), '');
  const line = endLine('bash', { command: 'false' }, true, result, false);
  assert.equal(line.event, 'PostToolUseFailure');
  assert.equal(line.payload.error, t);
});

test('Stop / StopFailure / aborted mapping', () => {
  const msgs = [
    { role: 'assistant', content: [{ type: 'text', text: 'old' }], stopReason: 'stop' },
    {
      role: 'assistant',
      content: [{ type: 'text', text: 'a' }, { type: 'toolCall' }, { type: 'text', text: 'b' }],
      stopReason: 'stop',
    },
  ];
  assert.deepEqual(stopLine(lastAssistant(msgs)), {
    event: 'Stop',
    payload: { last_assistant_message: 'a\n\nb' },
  });
  assert.deepEqual(stopLine({ text: 'partial', stopReason: 'aborted' }), {
    event: 'Stop',
    payload: {},
  });
  const err = stopLine({ text: '', stopReason: 'error', errorMessage: 'e'.repeat(2000) });
  assert.equal(err.event, 'StopFailure');
  assert.equal((err.payload.error as string).length, 1024);
  assert.deepEqual(stopLine(undefined), { event: 'Stop', payload: {} });
});

test('write create detection', () => {
  const created = endLine('write', { path: 'n.txt' }, false, {}, true);
  assert.deepEqual(created.payload.tool_response, { type: 'create' });
  const overwrite = endLine('write', { path: 'n.txt' }, false, {}, false);
  assert.equal(overwrite.event, 'PostToolUse');
  assert.equal('tool_response' in overwrite.payload, false);
});
