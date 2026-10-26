// Typed access to the native engine. Every mutating command resolves only after the engine
// confirmed it (the reply carries the updated project), so the UI never shows an unconfirmed
// state.

import { useEffect, useRef, useState } from 'react';
import NativeVoxEngine from '../native/NativeVoxEngine';
import type { DeviceInfo, EngineStatus, Project } from './types';

export class EngineError extends Error {}

export interface Reply {
  ok: boolean;
  error?: string;
  project?: Project;
  canUndo?: boolean;
  canRedo?: boolean;
  [key: string]: any;
}

export async function cmd<T extends Reply = Reply>(method: string, args: object = {}): Promise<T> {
  const text = await NativeVoxEngine.call(method, JSON.stringify(args));
  let reply: T;
  try {
    reply = JSON.parse(text);
  } catch {
    throw new EngineError('Invalid reply from the audio engine');
  }
  if (!reply.ok) throw new EngineError(reply.error || `${method} failed`);
  return reply;
}

export async function pickAudioFile(): Promise<{ path: string; name: string; size: number } | null> {
  const r = await NativeVoxEngine.pickAudioFile();
  return r ? JSON.parse(r) : null;
}

export const saveFileAs = (path: string, name: string, mime = 'audio/wav') => NativeVoxEngine.saveFileAs(path, name, mime);
export const shareFile = (path: string, mime = 'audio/wav') => NativeVoxEngine.shareFile(path, mime);
export async function getDeviceInfo(): Promise<DeviceInfo> {
  return JSON.parse(await NativeVoxEngine.getAudioDeviceInfo());
}

//------------------------------------------------------------------------------------------
// Status polling: one ~30 Hz loop shared by every subscriber, only while someone listens.

type Listener = (s: EngineStatus) => void;
const listeners = new Set<Listener>();
let timer: ReturnType<typeof setInterval> | null = null;
let last: EngineStatus | null = null;

function tick() {
  try {
    const s = JSON.parse(NativeVoxEngine.getStatus()) as EngineStatus;
    if (!s || s.position === undefined) return;
    last = s;
    listeners.forEach(l => l(s));
  } catch {
    // engine not ready yet
  }
}

export function subscribeStatus(l: Listener): () => void {
  listeners.add(l);
  if (!timer) timer = setInterval(tick, 33);
  if (last) l(last);
  return () => {
    listeners.delete(l);
    if (listeners.size === 0 && timer) {
      clearInterval(timer);
      timer = null;
    }
  };
}

/** Re-renders only when the selected value changes. */
export function useStatus<T>(select: (s: EngineStatus) => T, initial: T): T {
  const [value, setValue] = useState<T>(initial);
  const selectRef = useRef(select);
  selectRef.current = select;
  useEffect(
    () =>
      subscribeStatus(s => {
        const v = selectRef.current(s);
        setValue(prev => (Object.is(prev, v) || JSON.stringify(prev) === JSON.stringify(v) ? prev : v));
      }),
    [],
  );
  return value;
}

//------------------------------------------------------------------------------------------
// formatting helpers

export const framesToSeconds = (frames: number, rate: number) => frames / Math.max(1, rate);

export function formatTime(seconds: number, tenths = false): string {
  const s = Math.max(0, seconds);
  const m = Math.floor(s / 60);
  const sec = Math.floor(s % 60);
  const base = `${m}:${sec.toString().padStart(2, '0')}`;
  return tenths ? `${base}.${Math.floor((s * 10) % 10)}` : base;
}

export function formatBytes(b: number): string {
  if (b >= 1e9) return `${(b / 1e9).toFixed(2)} GB`;
  if (b >= 1e6) return `${(b / 1e6).toFixed(1)} MB`;
  if (b >= 1e3) return `${(b / 1e3).toFixed(0)} KB`;
  return `${b} B`;
}

export const linToDb = (v: number) => (v > 1e-6 ? 20 * Math.log10(v) : -120);
