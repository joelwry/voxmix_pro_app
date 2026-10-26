// Effect chain editor for one destination (Layer FX, Bus FX or Master FX), the effect browser
// and the preset browser. Every control writes the real DSP parameter through the engine.

import React, { useEffect, useMemo, useRef, useState } from 'react';
import { Alert, Pressable, ScrollView, StyleSheet, Text, TextInput, View } from 'react-native';
import { cmd } from '../engine/engine';
import type { EffectCategory, EffectDescriptor, EffectSlot, FxTarget, ParamInfo } from '../engine/types';
import { chainFor, targetLabel, useApp } from '../state/AppState';
import { Button, Chip, DestinationBadge, Field, Header, IconButton, Sheet, Slider, Toggle } from '../ui/components';
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

function SlotCard({ target, slot, desc, index, count, accent }: {
  target: FxTarget; slot: EffectSlot; desc?: EffectDescriptor; index: number; count: number; accent: string;
}) {
  const { run } = useApp();
  const [open, setOpen] = useState(index === 0);
  const [values, setValues] = useState(slot.params);
  const [meter, setMeter] = useState(0);
  const tp = useThrottledParam(target, slot.id);
  useEffect(() => setValues(slot.params), [slot.params]);

  useEffect(() => {
    // live gain-reduction / detected-pitch readout while the card is open
    if (!open || !desc) return;
    const showMeter = ['compressor', 'deesser', 'limiter', 'gate', 'vocal_sweetener', 'pitch_correction', 'mastering'].includes(desc.type);
    if (!showMeter) return;
    const t = setInterval(() => cmd<any>('getEffectState', { target, slotId: slot.id }).then(r => setMeter(r.meter)).catch(() => {}), 150);
    return () => clearInterval(t);
  }, [open, desc, target, slot.id]);

  if (!desc) return <View style={s.slot}><Text style={s.dim}>Unknown effect “{slot.type}”</Text></View>;
  const meterText = desc.type === 'pitch_correction'
    ? (meter > 0 ? `Detected ${meter.toFixed(1)} Hz` : 'No pitch detected')
    : `Gain reduction ${meter.toFixed(1)} dB`;
  return (
    <View style={[s.slot, { borderLeftColor: accent }, slot.bypass && { opacity: 0.6 }]}>
      <View style={s.slotHead}>
        <Pressable style={{ flex: 1 }} accessibilityRole="button" accessibilityLabel={`${desc.name} settings`} onPress={() => setOpen(!open)}>
          <Text style={s.slotName}>{index + 1}. {desc.name}</Text>
          <Text style={s.dim} numberOfLines={1}>{slot.bypass ? 'Bypassed' : desc.origin}</Text>
        </Pressable>
        <Toggle label={`${desc.name} on`} value={!slot.bypass} onChange={on => run('setEffectBypass', { target, slotId: slot.id, bypass: !on })} />
        {target !== 'mastering' ? (
          <>
            <IconButton icon="up" label="Move up" disabled={index === 0} onPress={() => run('moveEffect', { target, slotId: slot.id, index: index - 1 })} />
            <IconButton icon="down" label="Move down" disabled={index === count - 1} onPress={() => run('moveEffect', { target, slotId: slot.id, index: index + 1 })} />
            <IconButton icon="close" label={`Remove ${desc.name}`} onPress={() => run('removeEffect', { target, slotId: slot.id })} />
          </>
        ) : null}
      </View>
      {open ? (
        <View style={{ marginTop: space.sm }}>
          <Text style={[s.dim, { marginBottom: space.sm }]}>{desc.description}</Text>
          {['compressor', 'deesser', 'limiter', 'gate', 'vocal_sweetener', 'pitch_correction'].includes(desc.type) ? (
            <Text style={s.meter}>{meterText}</Text>
          ) : null}
          {desc.params.map(p => (
            <ParamControl key={p.id} p={p} value={values[p.id] ?? p.default}
              onBegin={tp.begin}
              onChange={v => { setValues(old => ({ ...old, [p.id]: v })); tp.set(p.id, v); }}
              onEnd={() => { tp.end(); run('getProject', {}, { quiet: true }); }} />
          ))}
        </View>
      ) : null}
    </View>
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
      <View style={{ paddingHorizontal: space.lg, gap: space.sm }}>
        <DestinationBadge kind={dest.kind} name={dest.name} color={dest.color} />
        <Text style={s.dim}>
          {dest.kind === 'layer' ? 'These effects change only this layer.' : dest.kind === 'bus' ? 'These effects process every layer routed to this bus.'
            : 'These effects process the whole song: beat and all vocals.'}
        </Text>
        <View style={{ flexDirection: 'row', gap: space.sm }}>
          <Button small title="+ Add effect" style={{ flex: 1 }} onPress={() => setBrowser(true)} />
          <Button small title="Presets" variant="secondary" style={{ flex: 1 }} onPress={() => setPresetsOpen(true)} />
        </View>
      </View>
      <ScrollView contentContainerStyle={{ padding: space.lg, paddingBottom: 60 }}>
        {chain.length === 0 ? <Text style={s.dim}>No effects yet. Add one, or start from a preset.</Text> : null}
        {chain.map((slot, i) => (
          <SlotCard key={slot.id} target={target} slot={slot} desc={describe(slot.type)} index={i} count={chain.length} accent={accent} />
        ))}
        {chain.length > 0 ? <Text style={[s.dim, { marginTop: space.sm }]}>Processing order: top to bottom.</Text> : null}
      </ScrollView>
      <EffectBrowser visible={browser} onClose={() => setBrowser(false)} target={target} />
      <PresetBrowser visible={presetsOpen} onClose={() => setPresetsOpen(false)} target={target} />
    </View>
  );
}

export function EffectBrowser({ visible, onClose, target }: { visible: boolean; onClose: () => void; target: FxTarget }) {
  const { catalog, run } = useApp();
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
          onPress={async () => { if (await run('addEffect', { target, type: d.type })) onClose(); }} style={s.browserRow}>
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
  slot: { backgroundColor: colors.panel, borderRadius: radius.md, padding: space.md, marginBottom: space.md, borderLeftWidth: 4, borderColor: colors.border },
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

