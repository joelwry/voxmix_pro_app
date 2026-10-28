// Types mirroring the engine's JSON (native/audio-engine/src/ProjectSerializer.cpp, Engine.cpp).

export type TrackKind = 'beat' | 'vocal';

export interface EffectSlot {
  id: string;
  type: string;
  bypass: boolean;
  params: Record<string, number>;
}

export interface Clip {
  id: string;
  sourceId: string;
  start: number; // frames on the timeline
  offset: number; // first frame used from the source
  length: number; // frames
  gainDb: number;
  fadeIn: number;
  fadeOut: number;
  name: string;
  muted: boolean;
}

export interface Track {
  id: string;
  name: string;
  kind: TrackKind;
  color: string;
  volumeDb: number;
  pan: number;
  mute: boolean;
  solo: boolean;
  output: string;
  inputChannel: number;
  fx: EffectSlot[];
  clips: Clip[];
}

export interface Bus {
  id: string;
  name: string;
  volumeDb: number;
  pan: number;
  mute: boolean;
  fx: EffectSlot[];
}

export interface Source {
  id: string;
  file: string;
  originalName: string;
  frames: number;
  channels: number;
  sampleRate: number;
  recorded: boolean;
}

export interface Project {
  format: 'voxmix-project';
  formatVersion: number;
  id: string;
  name: string;
  createdAt: number;
  modifiedAt: number;
  sampleRate: number;
  bpm: number;
  timeSignature: [number, number];
  recordOffsetFrames: number;
  inputMonitoring: boolean;
  recording: { micMode: 'raw' | 'voice' | 'auto' | 'clean'; latencyMode: 'lowest' | 'balanced' | 'safe'; inputLowCut: boolean };
  loop: { on: boolean; start: number; end: number };
  analysis: {
    done: boolean; sourceId: string; bpm: number; bpmConfidence: number;
    keyRoot: number; keyMinor: boolean; keyConfidence: number; altKeyRoot: number; altKeyMinor: boolean;
  };
  sources: Source[];
  tracks: Track[];
  buses: Bus[];
  master: {
    volumeDb: number;
    fx: EffectSlot[];
    masteringOn: boolean;
    mastering: EffectSlot;
    masteringPreset: string;
    loudnessTarget: number;
  };
  export: {
    format: string;
    bitDepth: number;
    sampleRate: number;
    channels: number;
    mp3Bitrate: number;
    includeTail: boolean;
  };
  meta: Record<string, string>;
}

export type ParamType = 'continuous' | 'integer' | 'toggle' | 'choice';

export interface ParamInfo {
  id: string;
  name: string;
  type: ParamType;
  min: number;
  max: number;
  default: number;
  unit: string;
  log: boolean;
  choices?: string[];
  group: string;
}

export type EffectCategory = 'pitch' | 'dynamics' | 'space' | 'creative' | 'chain' | 'mastering';

export interface EffectDescriptor {
  type: string;
  name: string;
  category: EffectCategory;
  description: string;
  origin: string;
  params: ParamInfo[];
}

export interface ChainPreset {
  id: string;
  name: string;
  category: string;
  description: string;
  factory: boolean;
  slots: EffectSlot[];
}

export interface MasteringPreset {
  id: string;
  name: string;
  description: string;
  loudnessTarget: number;
  params: Record<string, number>;
}

export interface ProjectSummary {
  id: string;
  name: string;
  modifiedAt: number;
  createdAt: number;
  seconds: number;
  layers: number;
  clips: number;
  beat: string;
  bpm: number;
}

export interface ExportResult {
  ok: boolean;
  error: string;
  path: string;
  frames: number;
  seconds: number;
  sampleRate: number;
  channels: number;
  bitDepth: number;
  peakDb: number;
  truePeakDb: number;
  integratedLufs: number;
  clipped: boolean;
  fileBytes: number;
  validated: boolean;
  validationNote: string;
  cancelled: boolean;
}

export interface MeterReading {
  l: number;
  r: number;
  clip: boolean;
}

export interface EngineStatus {
  position: number;
  sampleRate: number;
  playing: boolean;
  recording: boolean;
  cpu: number;
  callbacks: number;
  bufferFrames: number;
  tracks: Array<MeterReading & { id: string }>;
  master?: MeterReading;
  loudness: { momentary: number; shortTerm: number; integrated: number; truePeak: number };
  record?: { frames: number; dropped: number; inputPeak: number };
  input?: { open: boolean; peak: number; clipped: boolean };
  overloads?: number;
  export: { running: boolean; progress: number; result?: ExportResult };
  deviceError?: string;
}

export interface DeviceInfo {
  nativeSampleRate: number;
  framesPerBuffer: number;
  lowLatency: boolean;
  proAudio: boolean;
  sdk: number;
  model: string;
  bluetooth: boolean;
  wiredHeadphones: boolean;
  usbAudio: boolean;
}

/** Effect destinations: a layer, a bus, the whole mix, or the mastering chain. */
export type FxTarget = `track:${string}` | `bus:${string}` | 'master' | 'mastering';

export const KEY_NAMES = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'];
export const keyLabel = (root: number, minor: boolean) => (root < 0 ? 'Unknown' : `${KEY_NAMES[root]} ${minor ? 'minor' : 'major'}`);

export interface TakeHealth { droppedFrames: number; inputOverruns: number; cpuOverloads: number; clipped: boolean; clean: boolean }
