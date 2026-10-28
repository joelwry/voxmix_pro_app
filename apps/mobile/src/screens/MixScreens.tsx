// Layer management, Mastering workspace, Export & share, Project settings (incl. audio device
// and latency calibration).

import React, { useEffect, useState } from 'react';
import { Pressable, ScrollView, StyleSheet, Text, View } from 'react-native';
import { cmd, formatBytes, formatTime, getDeviceInfo, Reply, saveFileAs, shareFile, useStatus } from '../engine/engine';
import type { DeviceInfo, ExportResult, MasteringPreset } from '../engine/types';
import { KEY_NAMES, keyLabel } from '../engine/types';
import { useApp } from '../state/AppState';
import { Button, Card, Chip, DestinationBadge, Field, Header, IconButton, LevelMeter, Progress, SectionTitle, Slider, Toggle } from '../ui/components';
import { colors, font, radius, space } from '../ui/theme';
import { EffectControls } from './EffectsScreen';
import { TransportBar } from '../ui/Transport';
import { TrackSheet } from './StudioScreen';

//------------------------------------------------------------------------------------------ layers

function LayerRow({ id }: { id: string }) {
  const m = useStatus(s => { const t = s.tracks?.find(x => x.id === id); return t ? [Math.round(t.l * 50) / 50, Math.round(t.r * 50) / 50, t.clip ? 1 : 0] : [0, 0, 0]; }, [0, 0, 0]);
  return <View style={{ width: 70 }}><LevelMeter l={m[0]} r={m[1]} clip={!!m[2]} height={5} /></View>;
}

export function LayersScreen() {
  const { project, run, back, navigate, selectTrack, selectedTrackId } = useApp();
  const [sheet, setSheet] = useState<string | null>(null);
  if (!project) return null;
  const vocals = project.tracks.filter(t => t.kind === 'vocal').length;
  return (
    <View style={{ flex: 1 }}>
      <Header title="Layers" subtitle={`${vocals} of 10 vocal layers`} onBack={back} />
      <ScrollView contentContainerStyle={{ padding: space.lg, paddingBottom: 40 }}>
        {project.tracks.map((t, i) => (
          <Pressable key={t.id} onPress={() => t.kind === 'vocal' && selectTrack(t.id)} accessibilityRole="button"
            accessibilityLabel={`${t.name}${t.id === selectedTrackId ? ', selected' : ''}`}
            style={[st.layer, { borderLeftColor: t.color }, t.id === selectedTrackId && { backgroundColor: colors.panelHigh }]}>
            <View style={[st.num, { backgroundColor: t.color }]}><Text style={st.numText}>{t.kind === 'beat' ? 'B' : i + (project.tracks[0]?.kind === 'beat' ? 0 : 1)}</Text></View>
            <View style={{ flex: 1 }}>
              <Text style={[st.name, t.mute && { color: colors.textFaint }]} numberOfLines={1}>{t.name}</Text>
              <Text style={st.dim}>{t.clips.length} {t.clips.length === 1 ? 'clip' : 'clips'} · {t.fx.length} FX · {t.volumeDb.toFixed(1)} dB{t.output !== 'master' ? ' · → bus' : ''}</Text>
            </View>
            <LayerRow id={t.id} />
            <Pressable onPress={() => run('updateTrack', { trackId: t.id, mute: !t.mute }, { quiet: true })} accessibilityLabel={`Mute ${t.name}`}
              style={[st.ms, t.mute && { backgroundColor: colors.amber }]}><Text style={st.msText}>M</Text></Pressable>
            <Pressable onPress={() => run('updateTrack', { trackId: t.id, solo: !t.solo }, { quiet: true })} accessibilityLabel={`Solo ${t.name}`}
              style={[st.ms, t.solo && { backgroundColor: colors.green }]}><Text style={st.msText}>S</Text></Pressable>
            <IconButton icon="more" label={`${t.name} options`} onPress={() => setSheet(t.id)} />
          </Pressable>
        ))}
        <Button title="+ Add Layer (up to 10)" disabled={vocals >= 10} onPress={() => run('addTrack')} style={{ marginTop: space.sm }} />

        <SectionTitle right={<Button small title="+ Bus" variant="secondary" disabled={project.buses.length >= 4} onPress={() => run('addBus')} />}>Buses</SectionTitle>
        <Text style={st.dim}>Route several layers to a bus to share compression or reverb (open a layer's options to choose its output).</Text>
        {project.buses.map(b => (
          <View key={b.id} style={[st.layer, { borderLeftColor: colors.busFx }]}>
            <View style={{ flex: 1 }}>
              <Text style={st.name}>{b.name}</Text>
              <Text style={st.dim}>{project.tracks.filter(t => t.output === b.id).length} layers · {b.fx.length} FX</Text>
            </View>
            <Button small title="Bus FX" variant="secondary" onPress={() => navigate({ name: 'effects', target: `bus:${b.id}` })} />
            <IconButton icon="trash" label={`Remove ${b.name}`} onPress={() => run('removeBus', { busId: b.id })} />
          </View>
        ))}
      </ScrollView>
      <TrackSheet track={project.tracks.find(t => t.id === sheet) ?? null} onClose={() => setSheet(null)} />
    </View>
  );
}

//------------------------------------------------------------------------------------------ mastering

function LoudnessPanel() {
  const l = useStatus(s => [s.loudness.momentary, s.loudness.shortTerm, s.loudness.integrated, s.loudness.truePeak].map(v => Math.round(v * 10) / 10), [-144, -144, -144, -144]);
  const clip = useStatus(s => !!s.master?.clip, false);
  const m = useStatus(s => [Math.round((s.master?.l ?? 0) * 50) / 50, Math.round((s.master?.r ?? 0) * 50) / 50], [0, 0]);
  const fmt = (v: number) => (v <= -100 ? '—' : v.toFixed(1));
  return (
    <Card>
      <View style={st.kvRow}>
        {[['Momentary', fmt(l[0]) + ' LUFS'], ['Short-term', fmt(l[1]) + ' LUFS'], ['Integrated', fmt(l[2]) + ' LUFS'], ['True peak', fmt(l[3]) + ' dBTP']].map(([k, v]) => (
          <View key={k} style={{ alignItems: 'center', flex: 1 }}><Text style={st.kvValue}>{v}</Text><Text style={st.dim}>{k}</Text></View>
        ))}
      </View>
      <View style={{ marginTop: space.sm }}><LevelMeter l={m[0]} r={m[1]} clip={clip} height={6} /></View>
      {clip ? <Text style={{ color: colors.record, marginTop: 4 }}>Clipping detected on the master output</Text> : null}
      <Text style={[st.dim, { marginTop: 4 }]}>Integrated loudness is measured since playback started.</Text>
    </Card>
  );
}

export function MasteringScreen() {
  const { project, run, back, describe } = useApp();
  const [presets, setPresets] = useState<MasteringPreset[]>([]);
  const [busy, setBusy] = useState(false);
  const [report, setReport] = useState<string>('');
  useEffect(() => { run<Reply & { presets: MasteringPreset[] }>('listMasteringPresets', {}, { quiet: true }).then(r => r && setPresets(r.presets)); }, [run]);
  if (!project) return null;
  const m = project.master;
  const desc = describe('mastering');
  return (
    <View style={{ flex: 1 }}>
      <Header title="Mastering" onBack={back} />
      <TransportBar compareTarget={m.masteringOn ? 'mastering' : undefined} compareLabel="Unmastered" />
      <ScrollView contentContainerStyle={{ padding: space.lg, paddingBottom: 60 }}>
        <DestinationBadge kind="mastering" name="Final master (after Master FX)" />
        <Card style={{ marginTop: space.md }}>
          <View style={st.kvRow}>
            <View style={{ flex: 1 }}>
              <Text style={st.name}>Mastering chain</Text>
              <Text style={st.dim}>EQ → glue compressor → stereo width → limiter. Your recordings are never changed.</Text>
            </View>
            <Toggle label="Mastering on" value={m.masteringOn} onChange={v => run('updateMaster', { masteringOn: v })} />
          </View>
          <Text style={[st.dim, { marginTop: space.sm }]}>
            Use A/B in the bar above to hear the unmastered mix while playing (loop a chorus with Loop). The comparison is not
            level-matched, so the louder version can sound better: judge with the loudness readings below.
          </Text>
        </Card>

        <SectionTitle>Starting point</SectionTitle>
        <View style={{ flexDirection: 'row', flexWrap: 'wrap' }}>
          {presets.map(p => <Chip key={p.id} label={p.name} active={m.masteringPreset === p.id} onPress={() => run('applyMasteringPreset', { presetId: p.id })} />)}
          {m.masteringPreset === 'custom' ? <Chip label="Custom" active /> : null}
        </View>
        <Text style={st.dim}>{presets.find(p => p.id === m.masteringPreset)?.description ?? 'Your own settings.'}</Text>

        <SectionTitle>Loudness match</SectionTitle>
        <Card>
          <Text style={st.dim}>
            Measures the whole song offline (ITU-R BS.1770) and sets the input gain so the master reaches the target; the limiter keeps peaks below the ceiling.
          </Text>
          <View style={{ flexDirection: 'row', flexWrap: 'wrap', marginTop: space.sm }}>
            {[-16, -14, -12, -9].map(t => <Chip key={t} label={`${t} LUFS`} active={Math.round(m.loudnessTarget) === t} onPress={() => run('updateMaster', { loudnessTarget: t })} />)}
          </View>
          <Button title="Match loudness" busy={busy} onPress={async () => {
            setBusy(true);
            const r = await run<any>('autoMaster', { target: m.loudnessTarget });
            setBusy(false);
            if (r) setReport(`Before ${r.before.integratedLufs.toFixed(1)} LUFS → after ${r.after.integratedLufs.toFixed(1)} LUFS, true peak ${r.after.truePeakDb.toFixed(1)} dBTP (input gain ${r.inputGain.toFixed(1)} dB)`);
          }} />
          <Button small title="Analyze mix" variant="ghost" style={{ marginTop: space.sm }} disabled={busy} onPress={async () => {
            setBusy(true);
            const a = await run<any>('analyzeMix', { mastering: false });
            const b = await run<any>('analyzeMix', { mastering: true });
            setBusy(false);
            if (a && b) setReport(`Unmastered ${a.integratedLufs.toFixed(1)} LUFS / ${a.truePeakDb.toFixed(1)} dBTP · Mastered ${b.integratedLufs.toFixed(1)} LUFS / ${b.truePeakDb.toFixed(1)} dBTP`);
          }} />
          {report ? <Text style={[st.kvValue, { marginTop: space.sm, textAlign: 'left' }]}>{report}</Text> : null}
        </Card>

        <SectionTitle>Meters</SectionTitle>
        <LoudnessPanel />

        <SectionTitle>Mastering controls</SectionTitle>
        {desc ? <EffectControls target="mastering" slot={m.mastering} desc={desc} accent={colors.mastering} /> : null}
        <Card style={{ marginTop: space.md }}>
          <Text style={st.dim}>Master volume (before the mastering chain)</Text>
          <Slider label="Master volume" value={m.volumeDb} min={-24} max={6} step={0.1}
            onChange={v => run('updateMaster', { volumeDb: v, quiet: true }, { quiet: true })} onEnd={() => run('getProject', {}, { quiet: true })} />
        </Card>
      </ScrollView>
    </View>
  );
}

//------------------------------------------------------------------------------------------ export

export function ExportScreen() {
  const { project, run, back, toast, replace } = useApp();
  const [name, setName] = useState(project?.name ?? 'My Song');
  const [bits, setBits] = useState(project?.export.bitDepth ?? 24);
  const [rate, setRate] = useState(project?.export.sampleRate ?? 0);
  const [channels, setChannels] = useState(project?.export.channels ?? 2);
  const [tail, setTail] = useState(project?.export.includeTail ?? true);
  const [est, setEst] = useState<{ seconds: number; bytes: number } | null>(null);
  const [result, setResult] = useState<ExportResult | null>(null);
  const [started, setStarted] = useState(false);
  const running = useStatus(s => s.export.running, false);
  const progress = useStatus(s => Math.round(s.export.progress * 100) / 100, 0);

  useEffect(() => {
    run<any>('estimateExport', { bitDepth: bits, sampleRate: rate || project?.sampleRate, channels }, { quiet: true }).then(r => r && setEst(r));
  }, [bits, rate, channels, run, project?.sampleRate]);

  useEffect(() => {
    if (!started || running) return;
    cmd<any>('getExportResult').then(r => { if (r.result) { setResult(r.result); setStarted(false); } }).catch(() => {});
  }, [started, running]);

  if (!project) return null;
  const doExport = async () => {
    setResult(null);
    await run('updateProject', { export: { bitDepth: bits, sampleRate: rate, channels, includeTail: tail } }, { quiet: true });
    const r = await run('exportMix', { name: name.trim() || project.name, bitDepth: bits, sampleRate: rate, channels, includeTail: tail });
    if (r) setStarted(true);
  };

  if (result?.ok) {
    return (
      <View style={{ flex: 1 }}>
        <Header title="Export" onBack={back} />
        <ScrollView contentContainerStyle={{ padding: space.xl, alignItems: 'stretch' }}>
          <View style={st.doneCircle}><Text style={{ color: colors.text, fontSize: 40 }}>✓</Text></View>
          <Text style={st.doneTitle}>Your Track is Ready!</Text>
          <Text style={[st.dim, { textAlign: 'center' }]}>
            {result.validated ? 'Exported and verified: the file was decoded again and matches the mix.' : `Exported. ${result.validationNote}`}
          </Text>
          <Card style={{ marginTop: space.lg }}>
            {[
              ['Duration', formatTime(result.seconds, true)],
              ['Format', `WAV ${result.bitDepth === 32 ? '32-bit float' : `${result.bitDepth}-bit`} · ${result.sampleRate / 1000} kHz · ${result.channels === 2 ? 'stereo' : 'mono'}`],
              ['Size', formatBytes(result.fileBytes)],
              ['Loudness', `${result.integratedLufs.toFixed(1)} LUFS`],
              ['Peak / true peak', `${result.peakDb.toFixed(1)} dBFS / ${result.truePeakDb.toFixed(1)} dBTP`],
            ].map(([k, v]) => <View key={k} style={st.kvRow}><Text style={st.dim}>{k}</Text><Text style={st.kvValue}>{v}</Text></View>)}
            {result.clipped ? <Text style={{ color: colors.amber, marginTop: 4 }}>The mix clips: turn mastering or the limiter on, or lower the master volume.</Text> : null}
            {result.validationNote && result.validated ? <Text style={{ color: colors.amber, marginTop: 4 }}>{result.validationNote}</Text> : null}
          </Card>
          <View style={{ gap: space.sm, marginTop: space.lg }}>
            <Button title="Save to device…" onPress={async () => {
              try { const n = await saveFileAs(result.path, `${name.trim() || project.name}.wav`); if (n) toast(`Saved ${n}`); } catch (e: any) { toast(e.message, 'error'); }
            }} />
            <Button title="Share" variant="secondary" onPress={() => shareFile(result.path).catch((e: any) => toast(e.message, 'error'))} />
            <Button title="Done" variant="ghost" onPress={back} />
            <Button title="Open Library" variant="ghost" onPress={() => replace({ name: 'library' })} />
          </View>
        </ScrollView>
      </View>
    );
  }

  return (
    <View style={{ flex: 1 }}>
      <Header title="Export" subtitle="Renders tracks, buses, Master FX and mastering" onBack={back} />
      <ScrollView contentContainerStyle={{ padding: space.lg, paddingBottom: 60 }}>
        <Field label="Song name" value={name} onChangeText={setName} maxLength={60} />
        <Text style={st.label}>Format</Text>
        <View style={st.chips}><Chip label="WAV" active /></View>
        <Text style={st.dim}>MP3 export is planned for a later version (it needs an encoder that passes the licence review).</Text>
        <Text style={st.label}>Bit depth</Text>
        <View style={st.chips}>{[16, 24, 32].map(b => <Chip key={b} label={b === 32 ? '32-bit float' : `${b}-bit`} active={bits === b} onPress={() => setBits(b)} />)}</View>
        <Text style={st.label}>Sample rate</Text>
        <View style={st.chips}>
          {[0, 44100, 48000].map(r => <Chip key={r} label={r === 0 ? `Project (${project.sampleRate / 1000} kHz)` : `${r / 1000} kHz`} active={rate === r} onPress={() => setRate(r)} />)}
        </View>
        <Text style={st.label}>Channels</Text>
        <View style={st.chips}>{[2, 1].map(c => <Chip key={c} label={c === 2 ? 'Stereo' : 'Mono'} active={channels === c} onPress={() => setChannels(c)} />)}</View>
        <View style={[st.kvRow, { marginTop: space.md }]}>
          <Text style={st.dim}>Include reverb/delay tails</Text>
          <Toggle label="Include tails" value={tail} onChange={setTail} />
        </View>
        <Card style={{ marginTop: space.md }}>
          <View style={st.kvRow}><Text style={st.dim}>Mastering</Text><Text style={st.kvValue}>{project.master.masteringOn ? 'On' : 'Off'}</Text></View>
          {est ? (
            <>
              <View style={st.kvRow}><Text style={st.dim}>Estimated duration</Text><Text style={st.kvValue}>{formatTime(est.seconds)}</Text></View>
              <View style={st.kvRow}><Text style={st.dim}>Estimated size</Text><Text style={st.kvValue}>{formatBytes(est.bytes)}</Text></View>
            </>
          ) : null}
        </Card>
        {running || started ? (
          <View style={{ marginTop: space.lg, gap: space.sm }}>
            <Text style={st.dim}>Rendering… {Math.round(progress * 100)}%</Text>
            <Progress value={progress} />
            <Button title="Cancel" variant="ghost" onPress={() => run('cancelExport')} />
          </View>
        ) : (
          <Button title="Export song" style={{ marginTop: space.lg }} onPress={doExport} />
        )}
        {result && !result.ok ? <Text style={{ color: result.cancelled ? colors.textDim : colors.danger, marginTop: space.md }}>{result.error}</Text> : null}
      </ScrollView>
    </View>
  );
}

//------------------------------------------------------------------------------------------ project settings

export function ProjectSettingsScreen() {
  const { project, run, back, toast } = useApp();
  const [analyzing, setAnalyzing] = useState(false);
  const [name, setName] = useState(project?.name ?? '');
  const [bpm, setBpm] = useState(String(project?.bpm ?? 120));
  const [offset, setOffset] = useState(project ? (project.recordOffsetFrames / project.sampleRate) * 1000 : 0);
  const [device, setDevice] = useState<any>(null);
  const [hw, setHw] = useState<DeviceInfo | null>(null);
  const [calib, setCalib] = useState<any>(null);
  const [testing, setTesting] = useState(false);
  useEffect(() => {
    run<any>('getInfo', {}, { quiet: true }).then(r => r && setDevice(r.device));
    getDeviceInfo().then(setHw).catch(() => {});
  }, [run]);
  if (!project) return null;
  const sr = project.sampleRate;
  const ms = (f: number) => `${((f / (device?.sampleRate || sr)) * 1000).toFixed(1)} ms`;

  const measure = async () => {
    setCalib(null);
    setTesting(true);
    const s = await run<any>('startLatencyTest');
    if (!s) { setTesting(false); return; }
    await new Promise<void>(res => setTimeout(() => res(), s.seconds * 1000 + 300));
    const r = await run<any>('finishLatencyTest', {});
    setTesting(false);
    if (r) setCalib(r);
  };

  return (
    <View style={{ flex: 1 }}>
      <Header title="Project Settings" onBack={back} />
      <ScrollView contentContainerStyle={{ padding: space.lg, paddingBottom: 60 }}>
        <Field label="Project name" value={name} onChangeText={setName} maxLength={60} />
        <Field label="Tempo (BPM)" value={bpm} onChangeText={t => setBpm(t.replace(/[^0-9.]/g, ''))} keyboardType="numeric" maxLength={5} />
        <Text style={st.label}>Time signature</Text>
        <View style={st.chips}>
          {[[4, 4], [3, 4], [6, 8], [2, 4]].map(([n, d]) => (
            <Chip key={`${n}/${d}`} label={`${n}/${d}`} active={project.timeSignature[0] === n && project.timeSignature[1] === d}
              onPress={() => run('updateProject', { timeSigNum: n, timeSigDen: d })} />
          ))}
        </View>
        <Button title="Save name & tempo" onPress={() => run('updateProject', { name: name.trim() || project.name, bpm: Number(bpm) || project.bpm }, { success: 'Saved' })} />

        <SectionTitle>Song key & tempo</SectionTitle>
        <Card>
          {project.analysis.done ? (
            <>
              <View style={st.kvRow}><Text style={st.dim}>Detected tempo</Text>
                <Text style={st.kvValue}>{project.analysis.bpm ? `${project.analysis.bpm} BPM (${Math.round(project.analysis.bpmConfidence * 100)}% sure)` : 'not detected'}</Text></View>
              <View style={st.kvRow}><Text style={st.dim}>Detected key</Text>
                <Text style={st.kvValue}>{project.analysis.keyRoot >= 0 ? `${keyLabel(project.analysis.keyRoot, project.analysis.keyMinor)} (${Math.round(project.analysis.keyConfidence * 100)}% sure)` : 'not detected'}</Text></View>
              {project.analysis.altKeyRoot >= 0 ? (
                <View style={st.kvRow}><Text style={st.dim}>Could also be</Text><Text style={st.kvValue}>{keyLabel(project.analysis.altKeyRoot, project.analysis.altKeyMinor)}</Text></View>
              ) : null}
            </>
          ) : <Text style={st.dim}>Import a beat: its tempo and key are detected automatically.</Text>}
          <Text style={st.dim}>Detection is an estimate: correct it below if it is wrong.</Text>
          <Text style={st.label}>Key</Text>
          <View style={st.chips}>
            {KEY_NAMES.map((k, i) => <Chip key={k} label={k} active={project.analysis.keyRoot === i} onPress={() => run('setKey', { root: i, minor: project.analysis.keyMinor })} />)}
          </View>
          <View style={st.chips}>
            <Chip label="Major" active={project.analysis.keyRoot >= 0 && !project.analysis.keyMinor} onPress={() => run('setKey', { root: Math.max(0, project.analysis.keyRoot), minor: false })} />
            <Chip label="Minor" active={project.analysis.keyRoot >= 0 && project.analysis.keyMinor} onPress={() => run('setKey', { root: Math.max(0, project.analysis.keyRoot), minor: true })} />
          </View>
          <View style={{ gap: space.sm, marginTop: space.sm }}>
            <Button small variant="secondary" title="Detect again (and use the detected tempo)" busy={analyzing} onPress={async () => {
              setAnalyzing(true);
              const r = await run<any>('analyzeBeat', { applyTempo: true });
              setAnalyzing(false);
              if (r) { setBpm(String(r.project.bpm)); toast(`${r.analysis.bpm} BPM · ${r.analysis.key || 'key unclear'}`); }
            }} />
            <Button small variant="secondary" title="Set every Pitch Correction to this key" disabled={project.analysis.keyRoot < 0}
              onPress={async () => { const r = await run<any>('applyKeyToPitchCorrection'); if (r) toast(`${r.changed} pitch correction ${r.changed === 1 ? 'effect' : 'effects'} updated`); }} />
          </View>
        </Card>

        <SectionTitle>Recording quality</SectionTitle>
        <Card>
          <Text style={st.name}>Microphone mode</Text>
          <View style={st.chips}>
            {([['raw', 'Studio (raw)'], ['voice', 'Voice'], ['auto', 'Auto level'], ['clean', 'Noise reduction']] as const).map(([id, label]) => (
              <Chip key={id} label={label} active={project.recording.micMode === id} onPress={() => run('updateProject', { micMode: id })} />
            ))}
          </View>
          <Text style={st.dim}>
            {{ raw: 'No phone processing: the most natural sound for singing. Keep a hand-width from the mic to avoid clipping.',
               voice: 'Light processing tuned for voice.',
               auto: 'The phone adjusts the level automatically (avoids clipping, but breathes on loud/soft parts).',
               clean: 'The phone removes background noise and echo. Can sound processed; use in noisy rooms.' }[project.recording.micMode]}
          </Text>
          <Text style={[st.name, { marginTop: space.md }]}>Latency mode</Text>
          <View style={st.chips}>
            {([['lowest', 'Lowest'], ['balanced', 'Balanced'], ['safe', 'Safe']] as const).map(([id, label]) => (
              <Chip key={id} label={label} active={project.recording.latencyMode === id} onPress={() => run('updateProject', { latencyMode: id })} />
            ))}
          </View>
          <Text style={st.dim}>If a take reports lost audio or you hear crackles, choose Safe: a bigger buffer, the timing of takes stays exact.</Text>
          <View style={[st.kvRow, { marginTop: space.md }]}>
            <View style={{ flex: 1 }}>
              <Text style={st.name}>Low cut on the microphone</Text>
              <Text style={st.dim}>Removes rumble, handling and wind noise below 40 Hz.</Text>
            </View>
            <Toggle label="Low cut" value={project.recording.inputLowCut} onChange={v => run('updateProject', { inputLowCut: v })} />
          </View>
        </Card>

        <SectionTitle>Recording</SectionTitle>
        <Card>
          <View style={st.kvRow}>
            <View style={{ flex: 1 }}>
              <Text style={st.name}>Input monitoring</Text>
              <Text style={st.dim}>Hear yourself while recording. Use headphones (feedback through the speaker). Turn off if the delay is distracting.</Text>
            </View>
            <Toggle label="Input monitoring" value={project.inputMonitoring} onChange={v => run('setMonitoring', { enabled: v })} />
          </View>
          <Text style={[st.label, { marginTop: space.md }]}>Recording offset: {offset.toFixed(1)} ms</Text>
          <Text style={st.dim}>Added to the automatic latency compensation. Negative moves new takes earlier.</Text>
          <Slider label="Recording offset" value={offset} min={-200} max={200} step={0.5} onChange={setOffset}
            onEnd={v => run('updateProject', { recordOffsetMs: v }, { quiet: true })} />
        </Card>

        <SectionTitle>Latency calibration</SectionTitle>
        <Card>
          <Text style={st.dim}>
            Unplug headphones, turn the media volume up and keep the room quiet. The phone plays 8 clicks through its speaker and records them with its
            microphone to measure the real round-trip delay. Takes about 5 seconds.
          </Text>
          <Button title={testing ? 'Measuring…' : 'Measure latency'} busy={testing} style={{ marginTop: space.md }} onPress={measure} />
          {calib ? (
            <View style={{ marginTop: space.md }}>
              <Text style={st.kvValue}>Measured round trip: {calib.measuredMs.toFixed(1)} ms ({calib.clicksDetected} clicks, {calib.consistent} consistent)</Text>
              <Text style={st.dim}>Device-reported: {calib.reportedMs.toFixed(1)} ms → suggested offset {calib.suggestedOffsetMs.toFixed(1)} ms</Text>
              {calib.reliable ? (
                <Button small title="Apply suggested offset" style={{ marginTop: space.sm }} onPress={async () => {
                  const r = await run<any>('updateProject', { recordOffsetMs: calib.suggestedOffsetMs }, { success: 'Offset applied' });
                  if (r) setOffset(calib.suggestedOffsetMs);
                }} />
              ) : <Text style={{ color: colors.amber, marginTop: 4 }}>The measurement was not consistent. Try again in a quieter place with higher volume.</Text>}
              <Text style={[st.dim, { marginTop: 4 }]}>Speaker-to-mic measurements do not include headphone latency (wired headphones add almost none; Bluetooth adds a lot).</Text>
            </View>
          ) : null}
        </Card>

        <SectionTitle>Audio device</SectionTitle>
        <Card>
          {device ? [
            ['Driver', `${device.api || '—'}${device.exclusive ? ' (exclusive)' : ' (shared)'}${device.lowLatency ? ', low latency' : ''}`],
            ['Engine sample rate', `${device.sampleRate || sr} Hz${hw && hw.nativeSampleRate !== (device.sampleRate || sr) ? ` (hardware ${hw.nativeSampleRate} Hz, resampled)` : ''}`],
            ['Buffer', `${device.bufferSize} frames (burst ${device.framesPerBurst})`],
            ['Output latency', `${ms(device.outputLatency)}${device.latencyMeasured ? ' measured' : ' estimated'}`],
            ['Input latency', device.inputOpen ? `${ms(device.inputLatency)}${device.latencyMeasured ? ' measured' : ' estimated'}` : 'microphone not open'],
            ['Underruns', String(device.xruns)],
          ].map(([k, v]) => <View key={k} style={st.kvRow}><Text style={st.dim}>{k}</Text><Text style={st.kvValue}>{v}</Text></View>) : <Text style={st.dim}>Audio not running</Text>}
          <Text style={[st.dim, { marginTop: space.sm }]}>Playback device: Android default output (follows headphones / USB automatically).</Text>
          <Button small title="Refresh" variant="ghost" style={{ marginTop: space.sm }} onPress={() => run<any>('getInfo', {}, { quiet: true }).then(r => r && setDevice(r.device))} />
        </Card>

        <SectionTitle>Storage</SectionTitle>
        <Card>
          <Text style={st.dim}>Project and recordings are stored in the app's private storage and saved automatically. {project.sources.length} audio files.</Text>
          <Button small title="Save now" variant="secondary" style={{ marginTop: space.sm }} onPress={() => run('saveProject', {}, { success: 'Project saved' })} />
        </Card>
      </ScrollView>
    </View>
  );
}

const st = StyleSheet.create({
  dim: { color: colors.textDim, fontSize: font.sm },
  name: { color: colors.text, fontSize: font.md, fontWeight: '600' },
  label: { color: colors.textDim, fontSize: font.sm, marginTop: space.md, marginBottom: 6 },
  chips: { flexDirection: 'row', flexWrap: 'wrap' },
  layer: { flexDirection: 'row', alignItems: 'center', gap: space.sm, backgroundColor: colors.panel, borderRadius: radius.md, padding: space.sm, marginBottom: space.sm, borderLeftWidth: 4, minHeight: 60 },
  num: { width: 28, height: 28, borderRadius: 14, alignItems: 'center', justifyContent: 'center' },
  numText: { color: colors.text, fontWeight: '800' },
  ms: { width: 30, height: 30, borderRadius: 6, backgroundColor: colors.border, alignItems: 'center', justifyContent: 'center' },
  msText: { color: colors.text, fontWeight: '800', fontSize: 12 },
  kvRow: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center', paddingVertical: 4, gap: space.md },
  kvValue: { color: colors.text, fontSize: font.sm, textAlign: 'right', flexShrink: 1 },
  doneCircle: { alignSelf: 'center', width: 96, height: 96, borderRadius: 48, borderWidth: 3, borderColor: colors.cyan, alignItems: 'center', justifyContent: 'center', marginTop: space.xl },
  doneTitle: { color: colors.text, fontSize: font.xl, fontWeight: '800', textAlign: 'center', marginTop: space.lg, marginBottom: space.sm },
  renameBar: { position: 'absolute', bottom: 0, left: 0, right: 0 },
});

