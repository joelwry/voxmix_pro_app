/**
 * Smoke tests: every screen renders against a fake engine and the main flows (onboarding,
 * new project, studio, effects, mastering, export, settings) do not crash.
 * The real engine is tested natively in native/audio-engine/tests.
 */
import React from 'react';
import ReactTestRenderer, { act } from 'react-test-renderer';

const project = {
  format: 'voxmix-project', formatVersion: 1, id: 'prj-1', name: 'Blessings (Remix)', createdAt: 0, modifiedAt: 0,
  sampleRate: 48000, bpm: 120, timeSignature: [4, 4], recordOffsetFrames: 0, inputMonitoring: false,
  recording: { micMode: 'raw', latencyMode: 'balanced', inputLowCut: true },
  loop: { on: false, start: 0, end: 0 },
  analysis: { done: true, sourceId: 'src-1', bpm: 100, bpmConfidence: 0.8, keyRoot: 9, keyMinor: true, keyConfidence: 0.7, altKeyRoot: 0, altKeyMinor: false },
  sources: [{ id: 'src-1', file: 'media/a.wav', originalName: 'beat.mp3', frames: 480000, channels: 2, sampleRate: 48000, recorded: false }],
  tracks: [
    { id: 'trk-b', name: 'Beat', kind: 'beat', color: '#38BDF8', volumeDb: 0, pan: 0, mute: false, solo: false, output: 'master', inputChannel: -1, fx: [],
      clips: [{ id: 'clp-1', sourceId: 'src-1', start: 0, offset: 0, length: 480000, gainDb: 0, fadeIn: 0, fadeOut: 0, name: 'beat.mp3', muted: false }] },
    { id: 'trk-1', name: 'Lead Vocal', kind: 'vocal', color: '#8B5CF6', volumeDb: 0, pan: 0, mute: false, solo: false, output: 'master', inputChannel: -1,
      fx: [{ id: 'fx-1', type: 'reverb', bypass: false, params: { decay: 2, mix: 25, type: 2 } }], clips: [] },
  ],
  buses: [],
  master: { volumeDb: 0, fx: [], masteringOn: false, mastering: { id: 'mastering', type: 'mastering', bypass: false, params: { ceiling: -1 } }, masteringPreset: 'default', loudnessTarget: -14 },
  export: { format: 'wav', bitDepth: 24, sampleRate: 0, channels: 2, mp3Bitrate: 320, includeTail: true },
  meta: {},
};

const catalog = [
  { type: 'reverb', name: 'Reverb', category: 'space', description: 'FDN reverb', origin: 'MoodShift FX', params: [
    { id: 'type', name: 'Type', type: 'choice', min: 0, max: 3, default: 2, unit: '', log: false, choices: ['Room', 'Plate', 'Hall', 'Cathedral'], group: 'Main' },
    { id: 'decay', name: 'Decay', type: 'continuous', min: 0.2, max: 15, default: 2.2, unit: 's', log: true, group: 'Main' },
    { id: 'mix', name: 'Mix', type: 'continuous', min: 0, max: 100, default: 25, unit: '%', log: false, group: 'Main' },
    { id: 'predelay', name: 'Pre-Delay', type: 'continuous', min: 0, max: 200, default: 20, unit: 'ms', log: false, group: 'Tone' }] },
  { type: 'mastering', name: 'Mastering Chain', category: 'mastering', description: '', origin: '', params: [
    { id: 'ceiling', name: 'Ceiling', type: 'continuous', min: -6, max: 0, default: -1, unit: 'dB', log: false, group: 'Finish' },
    { id: 'limiterOn', name: 'Limiter', type: 'toggle', min: 0, max: 1, default: 1, unit: '', log: false, group: 'Finish' }] },
];

const calls: string[] = [];
const reply = (method: string, args: any): any => {
  calls.push(method);
  switch (method) {
    case 'getSettings': return { settings: { onboarded: false } };
    case 'getEffectCatalog': return { effects: catalog };
    case 'listPresets': return { presets: [{ id: 'clean_lead', name: 'Clean Lead', category: 'vocal', description: 'x', factory: true, slots: [] }] };
    case 'listProjects': return { projects: [{ id: 'prj-1', name: 'Blessings (Remix)', modifiedAt: 1, createdAt: 1, seconds: 165, layers: 4, clips: 3, beat: 'beat.mp3', bpm: 120 }] };
    case 'listExports': return { exports: [] };
    case 'listMasteringPresets': return { presets: [{ id: 'default', name: 'Default', description: 'd', loudnessTarget: -14, params: {} }] };
    case 'getWaveform': return { peaks: new Array(args.buckets).fill(0.5) };
    case 'estimateExport': return { seconds: 10, bytes: 1000 };
    case 'getInfo': return { version: '0.1.0', device: { running: true, api: 'AAudio', sampleRate: 48000, bufferSize: 192, framesPerBurst: 96, outputLatency: 960, inputLatency: 480, latencyMeasured: true, xruns: 0, inputOpen: false } };
    case 'getEffectState': return { meter: 0, values: {} };
    case 'getExportResult': return { running: false };
    case 'listEffectPresets': return { presets: [] };
    case 'setCompare': return { comparing: !!args.enabled };
    default: return { project, canUndo: false, canRedo: false };
  }
};

jest.mock('react-native-safe-area-context', () => require('react-native-safe-area-context/jest/mock').default);

jest.mock('../src/native/NativeVoxEngine', () => ({
  __esModule: true,
  default: {
    call: (m: string, a: string) => Promise.resolve(JSON.stringify({ ok: true, ...reply(m, JSON.parse(a)) })),
    getStatus: () => JSON.stringify({ position: 48000, sampleRate: 48000, playing: false, recording: false, cpu: 0.1, callbacks: 1, bufferFrames: 192,
      tracks: [{ id: 'trk-1', l: 0.2, r: 0.2, clip: false }], master: { l: 0.3, r: 0.3, clip: false },
      loudness: { momentary: -14, shortTerm: -14, integrated: -14, truePeak: -1 }, export: { running: false, progress: 0 },
      input: { open: true, peak: 0.3, clipped: false } }),
    pickAudioFile: () => Promise.resolve(''),
    saveFileAs: () => Promise.resolve(''),
    shareFile: () => Promise.resolve(true),
    getAudioDeviceInfo: () => Promise.resolve(JSON.stringify({ nativeSampleRate: 48000, framesPerBuffer: 192, lowLatency: true, proAudio: false, sdk: 34, model: 'Test', bluetooth: false, wiredHeadphones: true, usbAudio: false })),
  },
}));

import App from '../App';

const flush = () => act(async () => { await new Promise<void>(r => setTimeout(() => r(), 0)); });

function press(root: ReactTestRenderer.ReactTestInstance, label: string) {
  const node = root.findAll(n => n.props.accessibilityLabel === label && typeof n.props.onPress === 'function')[0];
  if (!node) throw new Error(`No pressable labelled "${label}"`);
  return act(async () => { await node.props.onPress({ nativeEvent: { locationX: 10 } }); });
}

const textOf = (root: ReactTestRenderer.ReactTestInstance) =>
  root.findAll(n => (n.type as unknown) === 'Text').map(n => [].concat(n.props.children).join('')).join('|');

test('onboarding → home → studio → effects / mastering / export / settings', async () => {
  let tree!: ReactTestRenderer.ReactTestRenderer;
  await act(async () => { tree = ReactTestRenderer.create(<App />); });
  await flush();
  const root = tree.root;
  expect(textOf(root)).toContain('Get Started');

  await press(root, 'Get Started');
  await flush();
  expect(textOf(root)).toContain('My Projects');

  await press(root, 'Open Blessings (Remix)');
  await flush();
  expect(calls).toContain('openProject');
  expect(textOf(root)).toContain('Lead Vocal');
  expect(calls).toContain('startAudio');

  await press(root, 'Play');
  expect(calls).toContain('play');

  expect(textOf(root)).toContain('A minor');          // detected key in the header
  expect(textOf(root)).toContain('MIC');              // live microphone level

  await press(root, '+ Layer');
  await flush();
  expect(textOf(root)).toContain('Import a vocal file as a new layer');
  await press(root, 'Close');

  await press(root, 'Loop options');
  await flush();
  expect(textOf(root)).toContain('Loop 4 bars from the playhead');
  await press(root, 'Loop 4 bars from the playhead');
  expect(calls).toContain('setLoop');

  await press(root, 'Layer FX');
  await flush();
  expect(textOf(root)).toContain('LAYER FX');
  expect(textOf(root)).toContain('Reverb');
  await press(root, 'Open Reverb');               // the rack opens one effect at a time
  await flush();
  expect(textOf(root)).toContain('Decay');            // knob on the Main page
  expect(textOf(root)).toContain('Tone');             // second page tab
  await press(root, 'Compare: Effect off');
  expect(calls).toContain('setCompare');
  await press(root, 'Back');
  await press(root, 'Back');

  await press(root, 'Master FX');
  await flush();
  expect(textOf(root)).toContain('MASTER FX');
  await press(root, 'Back');

  await press(root, 'Mastering');
  await flush();
  expect(textOf(root)).toContain('Loudness match');
  await press(root, 'Back');

  await press(root, 'Export');
  await flush();
  expect(textOf(root)).toContain('Bit depth');
  await press(root, 'Back');

  await press(root, 'Layers');
  await flush();
  expect(textOf(root)).toContain('vocal layers');
  await press(root, 'Back');

  await press(root, 'Project settings');
  await flush();
  expect(textOf(root)).toContain('Latency calibration');
  expect(textOf(root)).toContain('Song key & tempo');
  expect(textOf(root)).toContain('Microphone mode');
  await act(async () => { tree.unmount(); });
}, 30000);
