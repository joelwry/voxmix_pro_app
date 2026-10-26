// Main multitrack studio: waveform timeline, transport, recording and clip editing.
// Positions come from the engine's audio frame clock (status polling); nothing here is timed by JS.

import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Alert, PanResponder, PermissionsAndroid, Pressable, ScrollView, StyleSheet, Text, View } from 'react-native';
import { cmd, formatTime, getDeviceInfo, pickAudioFile, useStatus } from '../engine/engine';
import type { Clip, Project, Track } from '../engine/types';
import { useApp } from '../state/AppState';
import { Button, Field, Header, Icon, IconButton, LevelMeter, Sheet, Slider, Waveform } from '../ui/components';
import { colors, font, radius, space } from '../ui/theme';

const LANE_H = 64;
const HEAD_W = 112;
const RULER_H = 26;

async function ensureMicPermission(): Promise<boolean> {
  const perm = PermissionsAndroid.PERMISSIONS.RECORD_AUDIO;
  if (await PermissionsAndroid.check(perm)) return true;
  const r = await PermissionsAndroid.request(perm, {
    title: 'Microphone access',
    message: 'VoxMix Pro records your voice into the selected layer. Audio stays on this phone unless you export and share it.',
    buttonPositive: 'Allow',
    buttonNegative: 'Not now',
  });
  return r === PermissionsAndroid.RESULTS.GRANTED;
}

//------------------------------------------------------------------------------------------ waveform cache
const waveCache = new Map<string, number[]>();

function ClipView({ clip, track, pps, rate, selected, onSelect, onMove, onTrim }: {
  clip: Clip; track: Track; pps: number; rate: number; selected: boolean;
  onSelect: () => void; onMove: (start: number) => void; onTrim: (edge: 'left' | 'right', frames: number) => void;
}) {
  const width = Math.max(4, (clip.length / rate) * pps);
  const buckets = Math.max(1, Math.min(800, Math.floor(width / 3)));
  const key = `${clip.id}:${clip.offset}:${clip.length}:${buckets}`;
  const [peaks, setPeaks] = useState<number[]>(waveCache.get(key) ?? []);
  const [dx, setDx] = useState(0);
  const [trim, setTrim] = useState<{ edge: 'left' | 'right'; dx: number } | null>(null);

  useEffect(() => {
    const cached = waveCache.get(key);
    if (cached) { setPeaks(cached); return; }
    let alive = true;
    cmd<any>('getWaveform', { clipId: clip.id, buckets }).then(r => {
      waveCache.set(key, r.peaks);
      if (alive) setPeaks(r.peaks);
    }).catch(() => {});
    return () => { alive = false; };
  }, [key, clip.id, buckets]);

  const ref = useRef({ selected, pps, rate, clip, onMove, onTrim });
  ref.current = { selected, pps, rate, clip, onMove, onTrim };

  const movePan = useMemo(() => PanResponder.create({
    onStartShouldSetPanResponder: () => false,
    onMoveShouldSetPanResponder: (_, g) => ref.current.selected && Math.abs(g.dx) > 6 && Math.abs(g.dx) > Math.abs(g.dy),
    onPanResponderTerminationRequest: () => false,
    onPanResponderMove: (_, g) => setDx(g.dx),
    onPanResponderRelease: (_, g) => {
      setDx(0);
      const { clip: c, pps: p, rate: r } = ref.current;
      ref.current.onMove(Math.max(0, Math.round(c.start + (g.dx / p) * r)));
    },
    onPanResponderTerminate: () => setDx(0),
  }), []);

  const trimPan = (edge: 'left' | 'right') => PanResponder.create({
    onStartShouldSetPanResponder: () => ref.current.selected,
    onMoveShouldSetPanResponder: () => ref.current.selected,
    onPanResponderTerminationRequest: () => false,
    onPanResponderMove: (_, g) => setTrim({ edge, dx: g.dx }),
    onPanResponderRelease: (_, g) => {
      setTrim(null);
      ref.current.onTrim(edge, Math.round((g.dx / ref.current.pps) * ref.current.rate));
    },
    onPanResponderTerminate: () => setTrim(null),
  });
  const leftPan = useMemo(() => trimPan('left'), []); // eslint-disable-line react-hooks/exhaustive-deps
  const rightPan = useMemo(() => trimPan('right'), []); // eslint-disable-line react-hooks/exhaustive-deps

  let left = (clip.start / rate) * pps + dx;
  let w = width;
  if (trim?.edge === 'left') { left += trim.dx; w -= trim.dx; }
  if (trim?.edge === 'right') w += trim.dx;

  return (
    <Pressable
      accessibilityRole="button"
      accessibilityLabel={`Clip ${clip.name || track.name}, ${formatTime(clip.start / rate)}`}
      onPress={onSelect}
      style={[st.clip, { left, width: Math.max(4, w), backgroundColor: track.color + '33', borderColor: selected ? colors.text : track.color + '99' }]}
      {...movePan.panHandlers}>
      <View style={{ position: 'absolute', left: 2, right: 2, top: 4 }}>
        <Waveform peaks={peaks} color={track.mute ? colors.textFaint : track.color} height={LANE_H - 14} />
      </View>
      {clip.gainDb !== 0 ? <Text style={st.clipGain}>{clip.gainDb > 0 ? '+' : ''}{clip.gainDb.toFixed(1)} dB</Text> : null}
      {selected ? (
        <>
          <View style={[st.handle, { left: -2 }]} {...leftPan.panHandlers} accessibilityLabel="Trim start" />
          <View style={[st.handle, { right: -2 }]} {...rightPan.panHandlers} accessibilityLabel="Trim end" />
        </>
      ) : null}
    </Pressable>
  );
}

function Playhead({ rate, pps, onFollow }: { rate: number; pps: number; onFollow: (x: number) => void }) {
  const pos = useStatus(s => s.position, 0);
  const playing = useStatus(s => s.playing, false);
  const x = (pos / rate) * pps;
  useEffect(() => { if (playing) onFollow(x); }, [x, playing, onFollow]);
  return <View pointerEvents="none" style={[st.playhead, { left: x }]} />;
}

function TimeDisplay({ rate }: { rate: number }) {
  const pos = useStatus(s => Math.floor(s.position / (rate / 10)), 0);
  return <Text style={st.time} accessibilityLabel="Playback position">{formatTime(pos / 10, true)}</Text>;
}

function TrackMeter({ id }: { id: string }) {
  const m = useStatus(s => {
    const t = s.tracks?.find(x => x.id === id);
    return t ? [Math.round(t.l * 100) / 100, Math.round(t.r * 100) / 100, t.clip ? 1 : 0] : [0, 0, 0];
  }, [0, 0, 0]);
  return <LevelMeter l={m[0]} r={m[1]} clip={!!m[2]} height={4} />;
}

function RecordingInfo({ rate }: { rate: number }) {
  const rec = useStatus(s => s.record ? [s.record.frames, s.record.dropped, Math.round(s.record.inputPeak * 50) / 50] : null, null as any);
  if (!rec) return null;
  return (
    <View style={st.recInfo} accessibilityLiveRegion="polite">
      <View style={st.recDot} />
      <Text style={st.recText}>REC {formatTime(rec[0] / rate, true)}</Text>
      <View style={{ width: 80 }}><LevelMeter l={rec[2]} r={rec[2]} height={5} /></View>
      {rec[1] > 0 ? <Text style={[st.recText, { color: colors.amber }]}>dropouts!</Text> : null}
    </View>
  );
}

function RecordingRegion({ project, trackIndex, pps }: { project: Project; trackIndex: number; pps: number }) {
  const rec = useStatus(s => (s.record ? s.position : -1), -1);
  const startRef = useRef<number | null>(null);
  if (rec < 0) { startRef.current = null; return null; }
  if (startRef.current === null) startRef.current = rec;
  const rate = project.sampleRate;
  const left = (startRef.current / rate) * pps;
  const width = Math.max(2, ((rec - startRef.current) / rate) * pps);
  return <View pointerEvents="none" style={[st.recRegion, { left, width, top: RULER_H + trackIndex * LANE_H + 4 }]} />;
}

//------------------------------------------------------------------------------------------ screen

export function StudioScreen() {
  const { project, run, navigate, back, canUndo, canRedo, toast, selectedTrackId, selectTrack } = useApp();
  const [pps, setPps] = useState(40);
  const [selectedClip, setSelectedClip] = useState<string | null>(null);
  const [trackMenu, setTrackMenu] = useState<Track | null>(null);
  const [busy, setBusy] = useState(false);
  const playing = useStatus(s => s.playing, false);
  const recording = useStatus(s => s.recording, false);
  const deviceError = useStatus(s => s.deviceError ?? '', '');
  const scrollRef = useRef<any>(null);
  const scrollX = useRef(0);
  const viewW = useRef(300);

  useEffect(() => {
    // start the audio device (playback only until the first recording asks for the microphone)
    run('startAudio', {}, { quiet: true }).then(r => { if (!r) toast('Audio output could not start. Check that no other app is using audio.', 'error'); });
    getDeviceInfo().then(i => { if (i.bluetooth) toast('Bluetooth headphones add latency. For tight timing use wired headphones.', 'info'); }).catch(() => {});
  }, [run, toast]);

  useEffect(() => { if (deviceError) toast(deviceError, 'error'); }, [deviceError, toast]);

  const tracks = project?.tracks ?? [];
  const vocal = tracks.filter(t => t.kind === 'vocal');
  const selected = tracks.find(t => t.id === selectedTrackId && t.kind === 'vocal') ?? vocal[0];
  useEffect(() => { if (selected && selected.id !== selectedTrackId) selectTrack(selected.id); }, [selected, selectedTrackId, selectTrack]);

  const rate = project?.sampleRate ?? 48000;
  const endSec = Math.max(60, ((project ? Math.max(0, ...tracks.flatMap(t => t.clips.map(c => c.start + c.length))) : 0) / rate) + 30);
  const contentW = endSec * pps;

  const follow = useCallback((x: number) => {
    if (x > scrollX.current + viewW.current * 0.85 || x < scrollX.current) {
      scrollRef.current?.scrollTo({ x: Math.max(0, x - viewW.current * 0.15), animated: false });
    }
  }, []);

  if (!project) return <View style={{ flex: 1 }}><Header title="Studio" onBack={back} /></View>;

  const clipObj = tracks.flatMap(t => t.clips.map(c => ({ c, t }))).find(x => x.c.id === selectedClip);
  const barSec = (60 / project.bpm) * project.timeSignature[0];
  const barStep = Math.max(1, Math.ceil(40 / (barSec * pps)));

  const record = async () => {
    if (recording) { setBusy(true); await run('stopRecording'); setBusy(false); return; }
    if (!selected) { toast('Add a vocal layer first', 'error'); return; }
    if (!(await ensureMicPermission())) {
      Alert.alert('Microphone needed', 'Recording needs microphone access. You can allow it in Android Settings → Apps → VoxMix Pro → Permissions.');
      return;
    }
    await run('startRecording', { trackId: selected.id });
  };

  const seekTo = (x: number) => run('seek', { frame: Math.max(0, Math.round((x / pps) * rate)) }, { quiet: true });

  return (
    <View style={{ flex: 1 }}>
      <Header
        title={project.name}
        subtitle={`${vocal.length} of 10 layers · ${project.bpm} BPM · ${project.sampleRate / 1000} kHz`}
        onBack={async () => { await run('closeProject'); back(); }}
        right={
          <View style={st.row}>
            <IconButton icon="undo" label="Undo" disabled={!canUndo || recording} onPress={() => run('undo')} />
            <IconButton icon="redo" label="Redo" disabled={!canRedo || recording} onPress={() => run('redo')} />
            <IconButton icon="gear" label="Project settings" onPress={() => navigate({ name: 'projectSettings' })} />
          </View>
        }
      />

      {/* transport */}
      <View style={st.transport}>
        <IconButton icon={playing && !recording ? 'pause' : 'play'} label={playing ? 'Pause' : 'Play'} size={18}
          style={st.tBtn} disabled={recording} onPress={() => run(playing ? 'pause' : 'play', {}, { quiet: true })} />
        <IconButton icon="stop" label="Stop" size={16} style={st.tBtn} onPress={() => run('stop', {}, { quiet: true })} />
        <Pressable accessibilityRole="button" accessibilityLabel={recording ? 'Stop recording' : `Record on ${selected?.name ?? 'layer'}`}
          onPress={busy ? undefined : record} style={[st.recBtn, recording && st.recBtnActive]}>
          {recording ? <Icon name="stop" size={20} /> : <Icon name="record" size={24} color={colors.text} />}
        </Pressable>
        <View style={{ flex: 1, alignItems: 'flex-end' }}>
          <TimeDisplay rate={rate} />
          <Text style={st.recTarget} numberOfLines={1}>{recording ? 'Recording' : 'Record to'}: {selected?.name ?? '—'}</Text>
        </View>
      </View>
      <RecordingInfo rate={rate} />

      {/* timeline */}
      <ScrollView style={{ flex: 1 }} contentContainerStyle={{ flexDirection: 'row' }}>
        <View style={{ width: HEAD_W }}>
          <View style={{ height: RULER_H }} />
          {tracks.map(t => {
            const isSel = t.id === selected?.id;
            return (
              <Pressable key={t.id} accessibilityRole="button" accessibilityLabel={`Layer ${t.name}${isSel ? ', selected for recording' : ''}`}
                onPress={() => t.kind === 'vocal' && selectTrack(t.id)} onLongPress={() => setTrackMenu(t)}
                style={[st.head, { borderLeftColor: t.color }, isSel && st.headSel]}>
                <Text style={[st.headName, t.mute && { color: colors.textFaint }]} numberOfLines={1}>{t.name}</Text>
                <View style={st.row}>
                  <Pressable accessibilityRole="button" accessibilityLabel={`Mute ${t.name}`} accessibilityState={{ selected: t.mute }}
                    onPress={() => run('updateTrack', { trackId: t.id, mute: !t.mute }, { quiet: true })} style={[st.ms, t.mute && { backgroundColor: colors.amber }]}>
                    <Text style={st.msText}>M</Text>
                  </Pressable>
                  <Pressable accessibilityRole="button" accessibilityLabel={`Solo ${t.name}`} accessibilityState={{ selected: t.solo }}
                    onPress={() => run('updateTrack', { trackId: t.id, solo: !t.solo }, { quiet: true })} style={[st.ms, t.solo && { backgroundColor: colors.green }]}>
                    <Text style={st.msText}>S</Text>
                  </Pressable>
                  <IconButton icon="more" label={`${t.name} options`} size={16} style={{ minWidth: 30, minHeight: 30 }} onPress={() => setTrackMenu(t)} />
                </View>
                <TrackMeter id={t.id} />
              </Pressable>
            );
          })}
        </View>
        <ScrollView
          ref={scrollRef}
          horizontal
          onScroll={e => { scrollX.current = e.nativeEvent.contentOffset.x; }}
          scrollEventThrottle={32}
          onLayout={e => { viewW.current = e.nativeEvent.layout.width; }}>
          <View style={{ width: contentW, height: RULER_H + tracks.length * LANE_H + 8 }}>
            <Pressable accessibilityLabel="Timeline ruler: tap to move the playhead" onPress={e => seekTo(e.nativeEvent.locationX)} style={st.ruler}>
              {Array.from({ length: Math.ceil(endSec / barSec / barStep) }, (_, i) => i * barStep).map(b => (
                <View key={b} style={[st.tick, { left: b * barSec * pps }]}><Text style={st.tickText}>{b + 1}</Text></View>
              ))}
            </Pressable>
            {tracks.map((t, i) => (
              <Pressable key={t.id} onPress={e => { setSelectedClip(null); seekTo(e.nativeEvent.locationX); }}
                accessibilityLabel={`${t.name} lane`} style={[st.lane, { top: RULER_H + i * LANE_H }]}>
                {t.clips.map(c => (
                  <ClipView key={c.id} clip={c} track={t} pps={pps} rate={rate} selected={selectedClip === c.id}
                    onSelect={() => setSelectedClip(selectedClip === c.id ? null : c.id)}
                    onMove={start => run('moveClip', { clipId: c.id, start }, {})}
                    onTrim={(edge, frames) => edge === 'left'
                      ? run('trimClip', { clipId: c.id, start: c.start + frames })
                      : run('trimClip', { clipId: c.id, length: c.length + frames })} />
                ))}
              </Pressable>
            ))}
            <RecordingRegion project={project} trackIndex={tracks.findIndex(t => t.id === selected?.id)} pps={pps} />
            <Playhead rate={rate} pps={pps} onFollow={follow} />
          </View>
        </ScrollView>
      </ScrollView>

      {/* clip tools */}
      {clipObj ? (
        <View style={st.clipBar}>
          <Text style={st.clipBarTitle} numberOfLines={1}>{clipObj.c.name || clipObj.t.name} · {formatTime(clipObj.c.length / rate, true)}</Text>
          <View style={st.row}>
            <IconButton icon="split" label="Split at playhead" onPress={() => run('splitClip', { clipId: clipObj.c.id })} />
            <IconButton icon="copy" label="Duplicate clip" onPress={() => run('duplicateClip', { clipId: clipObj.c.id })} />
            <IconButton icon="minus" label="Clip gain down 1 dB" onPress={() => run('setClipGain', { clipId: clipObj.c.id, gainDb: clipObj.c.gainDb - 1 })} />
            <IconButton icon="plus" label="Clip gain up 1 dB" onPress={() => run('setClipGain', { clipId: clipObj.c.id, gainDb: clipObj.c.gainDb + 1 })} />
            <IconButton icon="trash" label="Delete clip" onPress={() => {
              const id = clipObj.c.id;
              Alert.alert('Delete clip?', 'The clip is removed from the timeline. Keep the recording file so you can undo?', [
                { text: 'Cancel', style: 'cancel' },
                { text: 'Remove clip', onPress: async () => { await run('deleteClip', { clipId: id }); setSelectedClip(null); } },
                { text: 'Delete recording too', style: 'destructive', onPress: async () => { await run('deleteClip', { clipId: id, deleteMedia: true }); setSelectedClip(null); } },
              ]);
            }} />
          </View>
          <Text style={st.hint}>Drag the clip to move it · drag its edges to trim</Text>
        </View>
      ) : null}

      {/* bottom actions */}
      <View style={st.bottom}>
        <View style={st.row}>
          <IconButton icon="minus" label="Zoom out" onPress={() => setPps(p => Math.max(8, p / 1.5))} />
          <IconButton icon="plus" label="Zoom in" onPress={() => setPps(p => Math.min(240, p * 1.5))} />
          <Button small title="+ Layer" variant="secondary" disabled={vocal.length >= 10 || recording}
            onPress={async () => { const r = await run<any>('addTrack'); if (r?.trackId) selectTrack(r.trackId); }} />
          <Button small title="Beat" variant="secondary" disabled={recording} onPress={async () => {
            try {
              const f = await pickAudioFile();
              if (f) { setBusy(true); await run('importAudio', { path: f.path, name: f.name }, { success: `Imported ${f.name}` }); setBusy(false); }
            } catch (e: any) { toast(e.message, 'error'); setBusy(false); }
          }} />
        </View>
        <View style={[st.row, { marginTop: space.sm }]}>
          <Button small style={{ flex: 1 }} title="Layer FX" disabled={!selected}
            onPress={() => selected && navigate({ name: 'effects', target: `track:${selected.id}` })} />
          <Button small style={{ flex: 1 }} variant="secondary" title="Master FX" onPress={() => navigate({ name: 'effects', target: 'master' })} />
          <Button small style={{ flex: 1 }} variant="secondary" title="Layers" onPress={() => navigate({ name: 'layers' })} />
        </View>
        <View style={[st.row, { marginTop: space.sm }]}>
          <Button small style={{ flex: 1 }} variant="secondary" title="Mastering" onPress={() => navigate({ name: 'mastering' })} />
          <Button small style={{ flex: 1 }} variant="secondary" title="Export" disabled={recording} onPress={() => navigate({ name: 'export' })} />
        </View>
      </View>

      <TrackSheet track={trackMenu} onClose={() => setTrackMenu(null)} />
    </View>
  );
}

/** Per-layer controls: volume, pan, rename shortcut, FX, duplicate, delete. */
export function TrackSheet({ track, onClose }: { track: Track | null; onClose: () => void }) {
  const { run, navigate, project } = useApp();
  const live = project?.tracks.find(t => t.id === track?.id) ?? track;
  const [vol, setVol] = useState(0);
  const [pan, setPan] = useState(0);
  const [name, setName] = useState('');
  useEffect(() => { if (live) { setVol(live.volumeDb); setPan(live.pan); setName(live.name); } }, [live?.id]); // eslint-disable-line react-hooks/exhaustive-deps
  if (!live) return null;
  const vocals = project?.tracks.filter(t => t.kind === 'vocal').length ?? 0;
  return (
    <Sheet visible={!!track} onClose={onClose} title={live.name}>
      <View style={[st.row, { alignItems: 'flex-end', gap: space.sm }]}>
        <View style={{ flex: 1 }}><Field label="Name" value={name} onChangeText={setName} maxLength={40} /></View>
        <Button small title="Rename" disabled={!name.trim() || name === live.name} style={{ marginBottom: space.md }}
          onPress={() => run('updateTrack', { trackId: live.id, name: name.trim() })} />
      </View>
      <Text style={st.label}>Color</Text>
      <View style={[st.row, { flexWrap: 'wrap' }]}>
        {['#8B5CF6', '#6366F1', '#EC4899', '#F59E0B', '#22C55E', '#06B6D4', '#3B82F6', '#EF4444', '#A855F7', '#14B8A6'].map(c => (
          <Pressable key={c} accessibilityLabel={`Color ${c}`} onPress={() => run('updateTrack', { trackId: live.id, color: c })}
            style={{ width: 30, height: 30, borderRadius: 15, backgroundColor: c, margin: 3, borderWidth: live.color === c ? 3 : 0, borderColor: colors.text }} />
        ))}
      </View>
      <Text style={st.label}>Volume {vol.toFixed(1)} dB</Text>
      <Slider label="Volume" value={vol} min={-60} max={12} step={0.1}
        onBegin={() => run('updateTrack', { trackId: live.id, commit: true, quiet: true }, { quiet: true })}
        onChange={v => { setVol(v); run('updateTrack', { trackId: live.id, volumeDb: v, quiet: true }, { quiet: true }); }}
        onEnd={() => run('getProject', {}, { quiet: true })} />
      <Text style={st.label}>Pan {pan === 0 ? 'C' : pan < 0 ? `L${Math.round(-pan * 100)}` : `R${Math.round(pan * 100)}`}</Text>
      <Slider label="Pan" value={pan} min={-1} max={1} step={0.01} color={colors.cyan}
        onBegin={() => run('updateTrack', { trackId: live.id, commit: true, quiet: true }, { quiet: true })}
        onChange={v => { setPan(v); run('updateTrack', { trackId: live.id, pan: v, quiet: true }, { quiet: true }); }}
        onEnd={() => run('getProject', {}, { quiet: true })} />
      {project && project.buses.length > 0 ? (
        <>
          <Text style={st.label}>Output</Text>
          <View style={[st.row, { flexWrap: 'wrap' }]}>
            {[{ id: 'master', name: 'Master' }, ...project.buses].map(b => (
              <Pressable key={b.id} onPress={() => run('updateTrack', { trackId: live.id, output: b.id })}
                style={[st.outChip, live.output === b.id && { borderColor: colors.cyan }]}>
                <Text style={{ color: colors.text }}>{b.name}</Text>
              </Pressable>
            ))}
          </View>
        </>
      ) : null}
      <View style={{ gap: space.sm, marginTop: space.lg }}>
        <Button title={`Layer FX (${live.fx.length})`} onPress={() => { onClose(); navigate({ name: 'effects', target: `track:${live.id}` }); }} />
        <Button title="Duplicate layer" variant="secondary" disabled={live.kind === 'vocal' && vocals >= 10}
          onPress={async () => { await run('duplicateTrack', { trackId: live.id }); onClose(); }} />
        <Button title="Delete layer" variant="danger" onPress={() => Alert.alert('Delete layer?', `“${live.name}” is removed from the project. Its recordings stay in the project folder and you can undo.`, [
          { text: 'Cancel', style: 'cancel' },
          { text: 'Delete', style: 'destructive', onPress: async () => { await run('removeTrack', { trackId: live.id }); onClose(); } },
        ])} />
      </View>
    </Sheet>
  );
}

const st = StyleSheet.create({
  row: { flexDirection: 'row', alignItems: 'center', gap: space.xs },
  transport: { flexDirection: 'row', alignItems: 'center', gap: space.md, paddingHorizontal: space.lg, paddingVertical: space.sm },
  tBtn: { backgroundColor: colors.panelHigh },
  recBtn: { width: 60, height: 60, borderRadius: 30, backgroundColor: colors.record, alignItems: 'center', justifyContent: 'center', borderWidth: 4, borderColor: '#FDA4AF55' },
  recBtnActive: { backgroundColor: '#9F1239', borderColor: colors.record },
  time: { color: colors.text, fontSize: font.xl, fontWeight: '700', fontVariant: ['tabular-nums'] },
  recTarget: { color: colors.textDim, fontSize: font.xs },
  recInfo: { flexDirection: 'row', alignItems: 'center', gap: space.sm, paddingHorizontal: space.lg, paddingBottom: space.xs },
  recDot: { width: 10, height: 10, borderRadius: 5, backgroundColor: colors.record },
  recText: { color: colors.record, fontWeight: '700', fontSize: font.sm },
  head: { height: LANE_H, paddingHorizontal: 6, paddingVertical: 4, borderLeftWidth: 4, borderBottomWidth: 1, borderColor: colors.border, backgroundColor: colors.panel, justifyContent: 'space-between' },
  headSel: { backgroundColor: colors.panelHigh },
  headName: { color: colors.text, fontSize: font.sm, fontWeight: '600' },
  ms: { width: 26, height: 22, borderRadius: 4, backgroundColor: colors.border, alignItems: 'center', justifyContent: 'center' },
  msText: { color: colors.text, fontSize: 11, fontWeight: '800' },
  ruler: { height: RULER_H, borderBottomWidth: 1, borderColor: colors.border },
  tick: { position: 'absolute', top: 0, height: RULER_H, borderLeftWidth: 1, borderColor: colors.border, paddingLeft: 3 },
  tickText: { color: colors.textFaint, fontSize: 10 },
  lane: { position: 'absolute', left: 0, right: 0, height: LANE_H, borderBottomWidth: 1, borderColor: colors.border },
  clip: { position: 'absolute', top: 3, height: LANE_H - 6, borderRadius: 6, borderWidth: 1, overflow: 'visible' },
  clipGain: { position: 'absolute', right: 4, top: 2, color: colors.text, fontSize: 9 },
  handle: { position: 'absolute', top: 0, bottom: 0, width: 18, backgroundColor: 'rgba(255,255,255,0.35)', borderRadius: 4 },
  playhead: { position: 'absolute', top: 0, bottom: 0, width: 2, backgroundColor: colors.text },
  recRegion: { position: 'absolute', height: LANE_H - 8, backgroundColor: 'rgba(244,63,94,0.35)', borderRadius: 6, borderWidth: 1, borderColor: colors.record },
  clipBar: { backgroundColor: colors.panelHigh, paddingHorizontal: space.md, paddingTop: space.xs, borderTopWidth: 1, borderColor: colors.border },
  clipBarTitle: { color: colors.text, fontSize: font.sm, fontWeight: '600' },
  hint: { color: colors.textFaint, fontSize: font.xs, paddingBottom: space.xs },
  bottom: { padding: space.md, backgroundColor: colors.bgElevated, borderTopWidth: 1, borderColor: colors.border },
  label: { color: colors.textDim, fontSize: font.sm, marginTop: space.md },
  outChip: { borderWidth: 1, borderColor: colors.border, borderRadius: radius.md, paddingHorizontal: space.md, paddingVertical: 8, marginRight: space.sm, marginTop: space.sm },
});
