// Effect chain editor for one destination (Layer FX, Bus FX or Master FX), the effect browser
// and the preset browser. Every control writes the real DSP parameter through the engine.

import React, { useEffect, useMemo, useRef, useState } from 'react';
import { Alert, Pressable, ScrollView, StyleSheet, Text, TextInput, View } from 'react-native';
import { cmd } from '../engine/engine';
import type { EffectCategory, EffectDescriptor, EffectSlot, FxTarget, ParamInfo } from '../engine/types';
import { chainFor, targetLabel, useApp } from '../state/AppState';
import { Button, Chip, DestinationBadge, Field, Header, IconButton, Knob, Sheet, Slider, Toggle } from '../ui/components';
import { TransportBar } from '../ui/Transport';
import { colors, font, radius, space } from '../ui/theme';

const CATEGORY_LABEL: Record<EffectCategory, string> = {
  pitch: 'Pitch & Tuning', chain: 'Vocal Chains', dynamics: 'EQ & Dynamics', space: 'Space & Ambience', creative: 'Creative', mastering: 'Mastering',
};

function formatValue(p: ParamInfo, v: number): string {
  if (p.type === 'toggle') return v >= 0.5 ? 'On' : 'Off';
  if (p.type === 'choice') return p.choices?.[Math.round(v)] ?? String(v);
  const abs = Math.abs(v);
  const txt = p.type === 'integer' ? String(Math.round(v)) : abs >= 1000 ? `${(v / 1000).toFixed(2)}k` : abs >= 100 ? v.toFixed(0) : abs >= 10 ? v.toFixed(1) : v.toFixed(2);
  return `${txt}${p.unit ? ' ' + p.unit : ''}`;
}

/** Sends parameter changes at most every 40 ms while dragging; the last value always goes out. */
function useThrottledParam(target: FxTarget, slotId: string) {
  const pending = useRef<Record<string, number>>({});
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const flush = () => {
    const params = pending.current;
    pending.current = {};
    timer.current = null;
    if (Object.keys(params).length) cmd('setEffectParams', { target, slotId, params }).catch(() => {});
  };
  return {
    set: (id: string, v: number) => {
      pending.current[id] = v;
      if (!timer.current) timer.current = setTimeout(flush, 40);
    },
    begin: () => cmd('setEffectParams', { target, slotId, params: {}, commit: true }).catch(() => {}),
    end: () => { if (timer.current) { clearTimeout(timer.current); flush(); } },
  };
}

export function ParamControl({ p, value, onChange, onBegin, onEnd }: {
  p: ParamInfo; value: number; onChange: (v: number) => void; onBegin: () => void; onEnd: () => void;
}) {
  if (p.type === 'toggle') {
    return (
      <View style={s.paramRow}>
        <Text style={s.paramName}>{p.name}</Text>
        <Toggle label={p.name} value={value >= 0.5} onChange={v => { onBegin(); onChange(v ? 1 : 0); onEnd(); }} />
      </View>
    );
  }
  if (p.type === 'choice' && p.choices) {
    return (
      <View style={{ marginBottom: space.sm }}>
        <Text style={s.paramName}>{p.name}</Text>
        <ScrollView horizontal showsHorizontalScrollIndicator={false} style={{ marginTop: 6 }}>
          {p.choices.map((c, i) => (
            <Chip key={c} label={c} active={Math.round(value) === i} onPress={() => { onBegin(); onChange(i); onEnd(); }} />
          ))}
        </ScrollView>
      </View>
    );
  }
  return (
    <View style={{ marginBottom: 2 }}>
      <View style={s.paramRow}>
        <Text style={s.paramName}>{p.name}</Text>
        <Text style={s.paramValue}>{formatValue(p, value)}</Text>
      </View>
      <Slider label={p.name} value={value} min={p.min} max={p.max} log={p.log && p.min > 0} step={p.type === 'integer' ? 1 : undefined}
        onBegin={onBegin} onChange={onChange} onEnd={onEnd} />
    </View>
  );
}

/** One page of an effect editor: up to 6 knobs plus that page's switches and choices. */
interface Page { title: string; params: ParamInfo[] }

function buildPages(desc: EffectDescriptor): Page[] {
  const groups: Page[] = [];
  for (const p of desc.params) {
    const title = p.group || 'Main';
    let g = groups.find(x => x.title === title);
    if (!g) { g = { title, params: [] }; groups.push(g); }
    g.params.push(p);
  }
  // keep every page short enough to fit a phone screen without scrolling
  const pages: Page[] = [];
  for (const g of groups) {
    const knobs = g.params.filter(p => p.type === 'continuous' || p.type === 'integer');
    const other = g.params.filter(p => p.type === 'toggle' || p.type === 'choice');
    if (knobs.length <= 6) { pages.push(g); continue; }
    for (let i = 0; i < knobs.length; i += 6) {
      pages.push({ title: i === 0 ? g.title : `${g.title} ${i / 6 + 1}`, params: [...(i === 0 ? other : []), ...knobs.slice(i, i + 6)] });
    }
  }
  return pages;
}

/** The control surface of one effect: page tabs, knobs, switches, choices. Every change goes
    straight to the DSP (throttled), undo snapshots once per gesture. */
export function EffectControls({ target, slot, desc, accent, onDrag }: {
  target: FxTarget; slot: EffectSlot; desc: EffectDescriptor; accent: string; onDrag?: (active: boolean) => void;
}) {
  const { run } = useApp();
  const pages = useMemo(() => buildPages(desc), [desc]);
  const [page, setPage] = useState(0);
  const [values, setValues] = useState<Record<string, number>>(slot.params);
  const tp = useThrottledParam(target, slot.id);
  useEffect(() => setValues(slot.params), [slot.params]);
  const current = pages[Math.min(page, pages.length - 1)];
  const knobs = current?.params.filter(p => p.type === 'continuous' || p.type === 'integer') ?? [];
  const switches = current?.params.filter(p => p.type === 'toggle') ?? [];
  const choices = current?.params.filter(p => p.type === 'choice') ?? [];
  const change = (id: string, v: number) => { setValues(o => ({ ...o, [id]: v })); tp.set(id, v); };
  const commitOne = (id: string, v: number) => { tp.begin(); change(id, v); tp.end(); run('getProject', {}, { quiet: true }); };

  return (
    <View>
      {pages.length > 1 ? (
        <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={s.tabs}>
          {pages.map((pg, i) => (
            <Pressable key={pg.title} accessibilityRole="tab" accessibilityState={{ selected: i === page }} onPress={() => setPage(i)}
              style={[s.tab, i === page && { backgroundColor: accent, borderColor: accent }]}>
              <Text style={[s.tabText, i === page && { color: colors.text }]}>{pg.title}</Text>
            </Pressable>
          ))}
        </ScrollView>
      ) : null}
      <View style={s.page}>
        {switches.length ? (
          <View style={s.switchRow}>
            {switches.map(p => {
              const on = (values[p.id] ?? p.default) >= 0.5;
              return (
                <Pressable key={p.id} accessibilityRole="switch" accessibilityState={{ checked: on }} accessibilityLabel={p.name}
                  onPress={() => commitOne(p.id, on ? 0 : 1)} style={[s.pill, on && { borderColor: accent, backgroundColor: accent + '44' }]}>
                  <View style={[s.led, on && { backgroundColor: colors.green }]} />
                  <Text style={[s.pillText, on && { color: colors.text }]}>{p.name.replace(/ On$/, '')}</Text>
                </Pressable>
              );
            })}
          </View>
        ) : null}
        {choices.map(p => (
          <View key={p.id} style={{ marginBottom: space.sm }}>
            <Text style={s.choiceLabel}>{p.name}</Text>
            <ScrollView horizontal showsHorizontalScrollIndicator={false}>
              {(p.choices ?? []).map((c, i) => (
                <Chip key={c} label={c} color={accent} active={Math.round(values[p.id] ?? p.default) === i} onPress={() => commitOne(p.id, i)} />
              ))}
            </ScrollView>
          </View>
        ))}
        <View style={s.knobGrid}>
          {knobs.map(p => (
            <Knob key={p.id} label={p.name} value={values[p.id] ?? p.default} min={p.min} max={p.max} color={accent}
              log={p.log && p.min > 0} step={p.type === 'integer' ? 1 : undefined} defaultValue={p.default}
              valueText={formatValue(p, values[p.id] ?? p.default)}
              onBegin={() => { onDrag?.(true); tp.begin(); }} onChange={v => change(p.id, v)}
              onEnd={() => { onDrag?.(false); tp.end(); run('getProject', {}, { quiet: true }); }} />
          ))}
        </View>
        {knobs.length ? <Text style={s.hint}>Drag a knob up or down · double tap to reset</Text> : null}
      </View>
    </View>
  );
}

const METER_TYPES = ['compressor', 'deesser', 'limiter', 'gate', 'vocal_sweetener', 'pitch_correction', 'mastering'];

function LiveMeter({ target, slotId, type }: { target: FxTarget; slotId: string; type: string }) {
  const [meter, setMeter] = useState(0);
  useEffect(() => {
    if (!METER_TYPES.includes(type)) return;
    const t = setInterval(() => cmd<any>('getEffectState', { target, slotId }).then(r => setMeter(r.meter)).catch(() => {}), 150);
    return () => clearInterval(t);
  }, [target, slotId, type]);
  if (!METER_TYPES.includes(type)) return null;
  if (type === 'pitch_correction') {
    const midi = meter > 0 ? 69 + 12 * Math.log2(meter / 440) : 0;
    const names = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'];
    const note = meter > 0 ? `${names[((Math.round(midi) % 12) + 12) % 12]}${Math.floor(Math.round(midi) / 12) - 1}` : '';
    const cents = meter > 0 ? Math.round((midi - Math.round(midi)) * 100) : 0;
    return <Text style={s.meter}>{meter > 0 ? `Singing ${note} (${cents >= 0 ? '+' : ''}${cents} ct) · ${meter.toFixed(0)} Hz` : 'Tuner: sing to see your pitch'}</Text>;
  }
  const gr = Math.max(0, Math.min(24, meter));
  return (
    <View style={{ marginBottom: space.sm }}>
      <Text style={s.meter}>Gain reduction {gr.toFixed(1)} dB</Text>
      <View style={s.grTrack}><View style={{ width: `${(gr / 24) * 100}%`, height: '100%', backgroundColor: colors.amber }} /></View>
    </View>
  );
}

export function EffectEditorScreen({ target, slotId }: { target: FxTarget; slotId: string }) {
  const { project, back, describe, run } = useApp();
  const [presets, setPresets] = useState<Array<{ id: string; name: string }>>([]);
  const [dragging, setDragging] = useState(false);
  const slot = chainFor(project, target).find(x => x.id === slotId);
  const desc = slot ? describe(slot.type) : undefined;
  useEffect(() => {
    if (slot) cmd<any>('listEffectPresets', { type: slot.type }).then(r => setPresets(r.presets)).catch(() => setPresets([]));
  }, [slot?.type]); // eslint-disable-line react-hooks/exhaustive-deps
  const dest = targetLabel(project, target);
  const accent = { layer: dest.color ?? colors.layerFx, bus: colors.busFx, master: colors.masterFx, mastering: colors.mastering }[dest.kind];
  if (!slot || !desc) return <View style={{ flex: 1 }}><Header title="Effect" onBack={back} /><Text style={[s.dim, { padding: space.lg }]}>This effect was removed.</Text></View>;
  return (
    <View style={{ flex: 1 }}>
      <Header title={desc.name} subtitle={desc.origin} onBack={back}
        right={<Toggle label={`${desc.name} on`} value={!slot.bypass} onChange={on => run('setEffectBypass', { target, slotId, bypass: !on })} />} />
      <TransportBar soloTrackId={dest.kind === 'layer' ? target.slice(6) : undefined} compareTarget={`slot:${slotId}`} compareLabel="Effect off" />
      <ScrollView contentContainerStyle={{ padding: space.lg, paddingBottom: 40 }} scrollEnabled={!dragging}>
        <DestinationBadge kind={dest.kind} name={dest.name} color={dest.color} />
        <Text style={[s.dim, { marginVertical: space.sm }]}>{desc.description}</Text>
        {slot.bypass ? <Text style={[s.meter, { color: colors.amber }]}>This effect is switched off (bypassed).</Text> : null}
        <LiveMeter target={target} slotId={slotId} type={slot.type} />
        {presets.length ? (
          <ScrollView horizontal showsHorizontalScrollIndicator={false} style={{ marginBottom: space.sm }}>
            {presets.map(p => <Chip key={p.id} label={p.name} color={accent} onPress={() => run('applyEffectPreset', { target, slotId, presetId: p.id }, { success: `${p.name} loaded` })} />)}
          </ScrollView>
        ) : null}
        <EffectControls target={target} slot={slot} desc={desc} accent={accent} onDrag={setDragging} />
        <View style={{ flexDirection: 'row', gap: space.sm, marginTop: space.md }}>
          <Button small variant="ghost" title="Reset to defaults" style={{ flex: 1 }}
            onPress={() => run('setEffectParams', { target, slotId, commit: true, params: Object.fromEntries(desc.params.map(p => [p.id, p.default])) }, { quiet: true }).then(() => run('getProject', {}, { quiet: true }))} />
          {target !== 'mastering' ? (
            <Button small variant="danger" title="Remove" style={{ flex: 1 }} onPress={async () => { if (await run('removeEffect', { target, slotId })) back(); }} />
          ) : null}
        </View>
      </ScrollView>
    </View>
  );
}

function SlotRow({ target, slot, desc, index, count, accent }: {
  target: FxTarget; slot: EffectSlot; desc?: EffectDescriptor; index: number; count: number; accent: string;
}) {
  const { run, navigate } = useApp();
  if (!desc) return <View style={s.slot}><Text style={s.dim}>Unknown effect “{slot.type}”</Text></View>;
  return (
    <Pressable accessibilityRole="button" accessibilityLabel={`Open ${desc.name}`} onPress={() => navigate({ name: 'effectEditor', target, slotId: slot.id })}
      style={({ pressed }) => [s.slot, { borderLeftColor: accent }, slot.bypass && { opacity: 0.55 }, pressed && { backgroundColor: colors.panelHigh }]}>
      <View style={[s.slotNum, { backgroundColor: accent }]}><Text style={s.slotNumText}>{index + 1}</Text></View>
      <View style={{ flex: 1 }}>
        <Text style={s.slotName} numberOfLines={1}>{desc.name}</Text>
        <Text style={s.dim} numberOfLines={1}>{slot.bypass ? 'Off' : CATEGORY_LABEL[desc.category]} · tap to edit</Text>
      </View>
      <Toggle label={`${desc.name} on`} value={!slot.bypass} onChange={on => run('setEffectBypass', { target, slotId: slot.id, bypass: !on })} />
      <IconButton icon="up" label="Move up" disabled={index === 0} onPress={() => run('moveEffect', { target, slotId: slot.id, index: index - 1 })} />
      <IconButton icon="down" label="Move down" disabled={index === count - 1} onPress={() => run('moveEffect', { target, slotId: slot.id, index: index + 1 })} />
    </Pressable>
  );
}

export function EffectsScreen({ target }: { target: FxTarget }) {
  const { project, back, describe } = useApp();
  const [browser, setBrowser] = useState(false);
  const [presetsOpen, setPresetsOpen] = useState(false);
  const dest = targetLabel(project, target);
  const chain = chainFor(project, target);
  const accent = { layer: dest.color ?? colors.layerFx, bus: colors.busFx, master: colors.masterFx, mastering: colors.mastering }[dest.kind];
  const title = { layer: 'Layer Effects', bus: 'Bus Effects', master: 'Master Effects', mastering: 'Mastering' }[dest.kind];
  return (
    <View style={{ flex: 1 }}>
      <Header title={title} onBack={back} />
      <TransportBar soloTrackId={dest.kind === 'layer' ? target.slice(6) : undefined} compareTarget={chain.length ? target : undefined}
        compareLabel={dest.kind === 'master' ? 'Master FX off' : 'FX off'} />
      <View style={{ paddingHorizontal: space.lg, paddingTop: space.md, gap: space.sm }}>
        <DestinationBadge kind={dest.kind} name={dest.name} color={dest.color} />
        <Text style={s.dim}>
          {dest.kind === 'layer' ? 'These effects change only this layer. Press play (and Loop) to hear them while you adjust.'
            : dest.kind === 'bus' ? 'These effects process every layer routed to this bus.'
            : 'These effects process the whole song: beat and all vocals.'}
        </Text>
        <View style={{ flexDirection: 'row', gap: space.sm }}>
          <Button small title="+ Add effect" style={{ flex: 1 }} onPress={() => setBrowser(true)} />
          <Button small title="Chain presets" variant="secondary" style={{ flex: 1 }} onPress={() => setPresetsOpen(true)} />
        </View>
      </View>
      <ScrollView contentContainerStyle={{ padding: space.lg, paddingBottom: 60 }}>
        {chain.length === 0 ? <Text style={s.dim}>No effects yet. Add one, or start from a preset.</Text> : null}
        {chain.map((slot, i) => (
          <SlotRow key={slot.id} target={target} slot={slot} desc={describe(slot.type)} index={i} count={chain.length} accent={accent} />
        ))}
        {chain.length > 0 ? <Text style={[s.dim, { marginTop: space.sm }]}>Signal flows top to bottom.</Text> : null}
      </ScrollView>
      <EffectBrowser visible={browser} onClose={() => setBrowser(false)} target={target} />
      <PresetBrowser visible={presetsOpen} onClose={() => setPresetsOpen(false)} target={target} />
    </View>
  );
}

export function EffectBrowser({ visible, onClose, target }: { visible: boolean; onClose: () => void; target: FxTarget }) {
  const { catalog, run, navigate } = useApp();
  const [q, setQ] = useState('');
  const [cat, setCat] = useState<EffectCategory | 'all'>('all');
  const list = useMemo(() => catalog.filter(d => d.category !== 'mastering')
    .filter(d => cat === 'all' || d.category === cat)
    .filter(d => !q || (d.name + ' ' + d.description).toLowerCase().includes(q.toLowerCase())), [catalog, q, cat]);
  return (
    <Sheet visible={visible} onClose={onClose} title="Add effect">
      <TextInput style={s.search} placeholder="Search effects…" placeholderTextColor={colors.textFaint} value={q} onChangeText={setQ} accessibilityLabel="Search effects" />
      <ScrollView horizontal showsHorizontalScrollIndicator={false} style={{ marginVertical: space.sm }}>
        {(['all', 'pitch', 'chain', 'dynamics', 'space', 'creative'] as const).map(c => (
          <Chip key={c} label={c === 'all' ? 'All' : CATEGORY_LABEL[c]} active={cat === c} onPress={() => setCat(c)} />
        ))}
      </ScrollView>
      {list.map(d => (
        <Pressable key={d.type} accessibilityRole="button" accessibilityLabel={`Add ${d.name}`}
          onPress={async () => { const r = await run<any>('addEffect', { target, type: d.type }); if (r) { onClose(); if (r.slotId) navigate({ name: 'effectEditor', target, slotId: r.slotId }); } }} style={s.browserRow}>
          <View style={{ flex: 1 }}>
            <Text style={s.slotName}>{d.name}</Text>
            <Text style={s.dim} numberOfLines={2}>{d.description}</Text>
          </View>
          <Text style={s.catTag}>{CATEGORY_LABEL[d.category]}</Text>
        </Pressable>
      ))}
    </Sheet>
  );
}

export function PresetBrowser({ visible, onClose, target }: { visible: boolean; onClose: () => void; target: FxTarget }) {
  const { presets, run, reloadPresets, describe } = useApp();
  const [q, setQ] = useState('');
  const [saveName, setSaveName] = useState('');
  const [renaming, setRenaming] = useState<{ id: string; name: string } | null>(null);
  const list = presets.filter(p => !q || (p.name + ' ' + p.description).toLowerCase().includes(q.toLowerCase()));
  const factory = list.filter(p => p.factory);
  const user = list.filter(p => !p.factory);
  const row = (p: typeof presets[number]) => (
    <View key={p.id} style={s.browserRow}>
      <Pressable style={{ flex: 1 }} accessibilityRole="button" accessibilityLabel={`Apply preset ${p.name}`}
        onPress={async () => { if (await run('applyPreset', { target, presetId: p.id }, { success: `${p.name} applied` })) onClose(); }}>
        <Text style={s.slotName}>{p.name}</Text>
        <Text style={s.dim} numberOfLines={2}>{p.description || p.slots.map(x => describe(x.type)?.name ?? x.type).join(' → ')}</Text>
      </Pressable>
      <IconButton icon="copy" label={`Duplicate ${p.name}`} onPress={async () => { await run('duplicatePreset', { presetId: p.id }); reloadPresets(); }} />
      {!p.factory ? (
        <>
          <IconButton icon="more" label={`Rename ${p.name}`} onPress={() => setRenaming({ id: p.id, name: p.name })} />
          <IconButton icon="trash" label={`Delete ${p.name}`} onPress={() => Alert.alert('Delete preset?', p.name, [
            { text: 'Cancel', style: 'cancel' },
            { text: 'Delete', style: 'destructive', onPress: async () => { await run('deletePreset', { presetId: p.id }); reloadPresets(); } },
          ])} />
        </>
      ) : null}
    </View>
  );
  return (
    <Sheet visible={visible} onClose={onClose} title="Presets">
      <Text style={[s.dim, { marginBottom: space.sm }]}>Applying a preset replaces this destination's effect chain (undo restores it).</Text>
      <TextInput style={s.search} placeholder="Search presets…" placeholderTextColor={colors.textFaint} value={q} onChangeText={setQ} accessibilityLabel="Search presets" />
      <Text style={s.group}>My presets</Text>
      {user.length ? user.map(row) : <Text style={s.dim}>None yet: save the current chain below.</Text>}
      <View style={{ flexDirection: 'row', gap: space.sm, alignItems: 'flex-end', marginTop: space.sm }}>
        <View style={{ flex: 1 }}><Field label="Save current chain as" value={saveName} onChangeText={setSaveName} placeholder="My Vocal" maxLength={40} /></View>
        <Button small title="Save" disabled={!saveName.trim()} style={{ marginBottom: space.md }}
          onPress={async () => { if (await run('savePreset', { target, name: saveName.trim() }, { success: 'Preset saved' })) { setSaveName(''); reloadPresets(); } }} />
      </View>
      <Text style={s.group}>Factory presets</Text>
      {factory.map(row)}
      <Sheet visible={!!renaming} onClose={() => setRenaming(null)} title="Rename preset">
        <Field label="Name" value={renaming?.name ?? ''} onChangeText={t => setRenaming(r => (r ? { ...r, name: t } : r))} maxLength={40} />
        <Button title="Rename" onPress={async () => { if (renaming) { await run('renamePreset', { presetId: renaming.id, name: renaming.name }); setRenaming(null); reloadPresets(); } }} />
      </Sheet>
    </Sheet>
  );
}

const s = StyleSheet.create({
  dim: { color: colors.textDim, fontSize: font.sm },
  slot: { flexDirection: 'row', alignItems: 'center', gap: space.sm, backgroundColor: colors.panel, borderRadius: radius.md, paddingVertical: space.sm, paddingLeft: space.md, paddingRight: 4, marginBottom: space.sm, borderLeftWidth: 4, borderColor: colors.border, minHeight: 64 },
  slotNum: { width: 26, height: 26, borderRadius: 13, alignItems: 'center', justifyContent: 'center' },
  slotNumText: { color: colors.text, fontWeight: '800', fontSize: font.sm },
  tabs: { gap: space.sm, paddingVertical: space.xs },
  tab: { paddingHorizontal: space.md, minHeight: 36, borderRadius: radius.lg, borderWidth: 1, borderColor: colors.border, justifyContent: 'center', backgroundColor: colors.panel },
  tabText: { color: colors.textDim, fontWeight: '700', fontSize: font.sm },
  page: { backgroundColor: colors.panel, borderRadius: radius.lg, padding: space.md, marginTop: space.sm, borderWidth: 1, borderColor: colors.border },
  switchRow: { flexDirection: 'row', flexWrap: 'wrap', gap: space.sm, marginBottom: space.md },
  pill: { flexDirection: 'row', alignItems: 'center', gap: 6, paddingHorizontal: space.md, minHeight: 36, borderRadius: radius.lg, borderWidth: 1, borderColor: colors.border, backgroundColor: colors.bgElevated },
  led: { width: 8, height: 8, borderRadius: 4, backgroundColor: colors.textFaint },
  pillText: { color: colors.textDim, fontSize: font.sm, fontWeight: '600' },
  choiceLabel: { color: colors.textDim, fontSize: font.sm, marginBottom: 6 },
  knobGrid: { flexDirection: 'row', flexWrap: 'wrap', justifyContent: 'space-around', rowGap: space.md },
  hint: { color: colors.textFaint, fontSize: font.xs, textAlign: 'center', marginTop: space.md },
  grTrack: { height: 6, borderRadius: 3, backgroundColor: colors.border, overflow: 'hidden', marginTop: 4 },
  slotHead: { flexDirection: 'row', alignItems: 'center', gap: 2 },
  slotName: { color: colors.text, fontSize: font.md, fontWeight: '700' },
  paramRow: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center', minHeight: 32 },
  paramName: { color: colors.textDim, fontSize: font.sm },
  paramValue: { color: colors.text, fontSize: font.sm, fontVariant: ['tabular-nums'] },
  meter: { color: colors.cyan, fontSize: font.sm, marginBottom: space.sm },
  search: { backgroundColor: colors.panel, borderRadius: radius.md, borderWidth: 1, borderColor: colors.border, color: colors.text, paddingHorizontal: space.md, minHeight: 44 },
  browserRow: { flexDirection: 'row', alignItems: 'center', gap: space.sm, paddingVertical: space.sm, borderBottomWidth: 1, borderColor: colors.border, minHeight: 56 },
  catTag: { color: colors.cyan, fontSize: font.xs },
  group: { color: colors.text, fontWeight: '700', fontSize: font.md, marginTop: space.lg, marginBottom: space.xs },
});

