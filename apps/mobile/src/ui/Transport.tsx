// Transport bar shown on the effect screens, the effect editor and mastering, so you can hear
// what an effect is doing while you adjust it: play/pause, back to start, loop a section,
// solo the layer being edited and an A/B compare that temporarily switches its effects off.

import React, { useEffect, useRef, useState } from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';
import { cmd, formatTime, useStatus } from '../engine/engine';
import NativeVoxEngine from '../native/NativeVoxEngine';
import type { Project } from '../engine/types';
import { useApp } from '../state/AppState';
import { Icon, IconButton } from './components';
import { colors, font, radius, space } from './theme';

/** A loop of `bars` bars starting at the bar that contains `frame`. */
export function barLoop(project: Project, frame: number, bars: number) {
  const barFrames = (60 / project.bpm) * project.timeSignature[0] * project.sampleRate;
  const start = Math.floor(frame / barFrames) * barFrames;
  return { start: Math.round(start), end: Math.round(start + bars * barFrames) };
}

function TimeText({ rate }: { rate: number }) {
  const tenths = useStatus(s => Math.floor(s.position / (rate / 10)), 0);
  return <Text style={st.time}>{formatTime(tenths / 10, true)}</Text>;
}

export function TransportBar({ soloTrackId, compareTarget, compareLabel = 'FX off' }: {
  soloTrackId?: string;      // shows a "Solo" toggle for this layer
  compareTarget?: string;    // shows an A/B toggle that bypasses this destination's effects
  compareLabel?: string;
}) {
  const { project, run } = useApp();
  const playing = useStatus(s => s.playing, false);
  const [comparing, setComparing] = useState(false);
  const comparingRef = useRef(false);
  comparingRef.current = comparing;

  // never leave a compare active when the screen closes
  useEffect(() => () => { if (comparingRef.current) cmd('setCompare', { enabled: false }).catch(() => {}); }, []);
  useEffect(() => {
    if (comparing && compareTarget) cmd('setCompare', { enabled: true, target: compareTarget }).catch(() => {});
  }, [compareTarget]); // eslint-disable-line react-hooks/exhaustive-deps

  if (!project) return null;
  const rate = project.sampleRate;
  const loopSet = project.loop.end > project.loop.start;
  const solo = soloTrackId ? project.tracks.find(t => t.id === soloTrackId)?.solo ?? false : false;

  const toggleLoop = async () => {
    if (project.loop.on) { await run('setLoop', { enabled: false }, { quiet: true }); return; }
    if (loopSet) { await run('setLoop', { enabled: true }); return; }
    const pos = JSON.parse(NativeVoxEngine.getStatus()).position ?? 0;
    const l = barLoop(project, pos, 4);
    await run('setLoop', { enabled: true, start: l.start, end: l.end });
  };

  return (
    <View style={st.bar}>
      <Pressable accessibilityRole="button" accessibilityLabel={playing ? 'Pause' : 'Play'} onPress={() => run(playing ? 'pause' : 'play', {}, { quiet: true })}
        style={[st.play, playing && { backgroundColor: colors.blue }]}>
        <Icon name={playing ? 'pause' : 'play'} size={16} />
      </Pressable>
      <IconButton icon="stop" label="Back to start" size={14} onPress={() => run('stop', {}, { quiet: true })} style={st.small} />
      <TimeText rate={rate} />
      <View style={{ flex: 1 }} />
      <Pressable accessibilityRole="switch" accessibilityState={{ checked: project.loop.on }} accessibilityLabel="Loop"
        onPress={toggleLoop} style={[st.chip, project.loop.on && st.chipOn]}>
        <Text style={[st.chipText, project.loop.on && st.chipTextOn]}>
          {project.loop.on ? `Loop ${formatTime(project.loop.start / rate)}–${formatTime(project.loop.end / rate)}` : 'Loop'}
        </Text>
      </Pressable>
      {soloTrackId ? (
        <Pressable accessibilityRole="switch" accessibilityState={{ checked: solo }} accessibilityLabel="Solo this layer"
          onPress={() => run('updateTrack', { trackId: soloTrackId, solo: !solo }, { quiet: true })} style={[st.chip, solo && { borderColor: colors.green, backgroundColor: '#14532D' }]}>
          <Text style={[st.chipText, solo && st.chipTextOn]}>Solo</Text>
        </Pressable>
      ) : null}
      {compareTarget ? (
        <Pressable accessibilityRole="switch" accessibilityState={{ checked: comparing }} accessibilityLabel={`Compare: ${compareLabel}`}
          onPress={async () => {
            const next = !comparing;
            setComparing(next);
            await cmd('setCompare', next ? { enabled: true, target: compareTarget } : { enabled: false }).catch(() => {});
          }}
          style={[st.chip, comparing && { borderColor: colors.amber, backgroundColor: '#78350F' }]}>
          <Text style={[st.chipText, comparing && st.chipTextOn]}>{comparing ? compareLabel : 'A/B'}</Text>
        </Pressable>
      ) : null}
    </View>
  );
}

const st = StyleSheet.create({
  bar: { flexDirection: 'row', alignItems: 'center', gap: 6, paddingHorizontal: space.md, paddingVertical: space.sm,
    backgroundColor: colors.bgElevated, borderBottomWidth: 1, borderColor: colors.border },
  play: { width: 40, height: 40, borderRadius: 20, backgroundColor: colors.purple, alignItems: 'center', justifyContent: 'center' },
  small: { minWidth: 36, minHeight: 36, backgroundColor: colors.panelHigh },
  time: { color: colors.text, fontSize: font.md, fontWeight: '700', fontVariant: ['tabular-nums'], marginLeft: 4 },
  chip: { paddingHorizontal: 10, minHeight: 34, borderRadius: radius.lg, borderWidth: 1, borderColor: colors.border, justifyContent: 'center', backgroundColor: colors.panel },
  chipOn: { borderColor: colors.cyan, backgroundColor: '#164E63' },
  chipText: { color: colors.textDim, fontSize: font.xs, fontWeight: '700' },
  chipTextOn: { color: colors.text },
});
