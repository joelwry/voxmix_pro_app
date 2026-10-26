// Shared, state-driven controls. Every control has a defined behaviour; disabled controls say
// why through their label or a hint rather than silently doing nothing.

import React, { useCallback, useRef, useState } from 'react';
import {
  ActivityIndicator,
  Modal,
  PanResponder,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  View,
  ViewStyle,
} from 'react-native';
import { colors, font, radius, space, touch } from './theme';

//------------------------------------------------------------------------------------------ icons
// Drawn with views (no icon font dependency) so they render identically on every device.

export function Icon({ name, size = 18, color = colors.text }: { name: string; size?: number; color?: string }) {
  const s = size;
  switch (name) {
    case 'play':
      return (
        <View style={{ width: s, height: s, alignItems: 'center', justifyContent: 'center' }}>
          <View style={{ width: 0, height: 0, marginLeft: s * 0.15, borderTopWidth: s * 0.42, borderBottomWidth: s * 0.42, borderLeftWidth: s * 0.72,
            borderTopColor: 'transparent', borderBottomColor: 'transparent', borderLeftColor: color }} />
        </View>
      );
    case 'pause':
      return (
        <View style={{ width: s, height: s, flexDirection: 'row', justifyContent: 'center', gap: s * 0.18 }}>
          <View style={{ width: s * 0.26, height: s, backgroundColor: color, borderRadius: 2 }} />
          <View style={{ width: s * 0.26, height: s, backgroundColor: color, borderRadius: 2 }} />
        </View>
      );
    case 'stop':
      return <View style={{ width: s * 0.8, height: s * 0.8, backgroundColor: color, borderRadius: 3 }} />;
    case 'record':
      return <View style={{ width: s, height: s, borderRadius: s / 2, backgroundColor: color }} />;
    default: {
      const glyph: Record<string, string> = {
        back: '‹', close: '×', plus: '+', minus: '−', more: '⋯', undo: '↶', redo: '↷', gear: '⚙', check: '✓',
        up: '▴', down: '▾', left: '◂', right: '▸', search: '⌕', mic: '◉', wave: '≋', star: '★', fx: 'ƒ',
        home: '⌂', library: '♫', layers: '☰', split: '⫶', copy: '⧉', trash: '⌫', save: '⤓', share: '⤴',
      };
      return <Text style={{ color, fontSize: s, lineHeight: s * 1.15, textAlign: 'center', minWidth: s }}>{glyph[name] ?? '•'}</Text>;
    }
  }
}

//------------------------------------------------------------------------------------------ buttons

export function Button({
  title, onPress, variant = 'primary', disabled, busy, style, icon, small,
}: {
  title: string;
  onPress?: () => void;
  variant?: 'primary' | 'secondary' | 'ghost' | 'danger' | 'record';
  disabled?: boolean;
  busy?: boolean;
  style?: ViewStyle;
  icon?: string;
  small?: boolean;
}) {
  const bg = { primary: colors.purple, secondary: colors.panelHigh, ghost: 'transparent', danger: colors.danger, record: colors.record }[variant];
  return (
    <Pressable
      accessibilityRole="button"
      accessibilityLabel={title}
      accessibilityState={{ disabled: !!disabled, busy: !!busy }}
      onPress={disabled || busy ? undefined : onPress}
      style={({ pressed }) => [
        styles.button,
        small && styles.buttonSmall,
        { backgroundColor: bg, opacity: disabled ? 0.4 : pressed ? 0.8 : 1 },
        variant === 'ghost' && { borderWidth: 1, borderColor: colors.border },
        variant === 'primary' && styles.glow,
        style,
      ]}>
      {busy ? <ActivityIndicator color={colors.text} /> : (
        <View style={styles.row}>
          {icon ? <Icon name={icon} size={small ? 14 : 16} /> : null}
          <Text style={[styles.buttonText, small && { fontSize: font.sm }]}>{title}</Text>
        </View>
      )}
    </Pressable>
  );
}

export function IconButton({
  icon, onPress, label, active, color, size = 20, disabled, style,
}: {
  icon: string; onPress?: () => void; label: string; active?: boolean; color?: string; size?: number; disabled?: boolean; style?: ViewStyle;
}) {
  return (
    <Pressable
      accessibilityRole="button"
      accessibilityLabel={label}
      hitSlop={6}
      onPress={disabled ? undefined : onPress}
      style={({ pressed }) => [styles.iconButton, active && { backgroundColor: colors.purpleDeep }, { opacity: disabled ? 0.35 : pressed ? 0.7 : 1 }, style]}>
      <Icon name={icon} size={size} color={color ?? colors.text} />
    </Pressable>
  );
}

export function Chip({ label, active, onPress, color, disabled }: { label: string; active?: boolean; onPress?: () => void; color?: string; disabled?: boolean }) {
  return (
    <Pressable
      accessibilityRole="button"
      accessibilityState={{ selected: !!active, disabled: !!disabled }}
      onPress={disabled ? undefined : onPress}
      style={[styles.chip, active && { borderColor: color ?? colors.cyan, backgroundColor: 'rgba(139,92,246,0.25)' }, disabled && { opacity: 0.4 }]}>
      <Text style={[styles.chipText, active && { color: colors.text }]}>{label}</Text>
    </Pressable>
  );
}

export function Toggle({ value, onChange, label, disabled }: { value: boolean; onChange: (v: boolean) => void; label?: string; disabled?: boolean }) {
  return (
    <Pressable
      accessibilityRole="switch"
      accessibilityLabel={label}
      accessibilityState={{ checked: value, disabled: !!disabled }}
      onPress={disabled ? undefined : () => onChange(!value)}
      hitSlop={10}
      style={[styles.toggle, value && { backgroundColor: colors.blue }, disabled && { opacity: 0.4 }]}>
      <View style={[styles.knob, value && { transform: [{ translateX: 20 }] }]} />
    </Pressable>
  );
}

//------------------------------------------------------------------------------------------ slider

/** Horizontal slider. onBegin fires once per gesture (used to snapshot undo), onChange while
    dragging (throttled by the caller if needed), onEnd on release. Supports log mapping. */
export function Slider({
  value, min, max, onChange, onBegin, onEnd, log, step, color = colors.purple, disabled, label,
}: {
  value: number; min: number; max: number;
  onChange: (v: number) => void; onBegin?: () => void; onEnd?: (v: number) => void;
  log?: boolean; step?: number; color?: string; disabled?: boolean; label?: string;
}) {
  const width = useRef(1);
  const startNorm = useRef(0);
  const latest = useRef(value);
  const toNorm = (v: number) => {
    if (log && min > 0) return Math.log(v / min) / Math.log(max / min);
    return (v - min) / (max - min || 1);
  };
  const fromNorm = (n: number) => {
    n = Math.min(1, Math.max(0, n));
    let v = log && min > 0 ? min * Math.pow(max / min, n) : min + n * (max - min);
    if (step) v = Math.round(v / step) * step;
    return Math.min(max, Math.max(min, v));
  };
  const cb = useRef({ onChange, onBegin, onEnd });
  cb.current = { onChange, onBegin, onEnd };
  const pan = useRef(
    PanResponder.create({
      onStartShouldSetPanResponder: () => true,
      onMoveShouldSetPanResponder: () => true,
      onPanResponderTerminationRequest: () => false,
      onPanResponderGrant: e => {
        cb.current.onBegin?.();
        const n = e.nativeEvent.locationX / width.current;
        startNorm.current = n;
        latest.current = fromNorm(n);
        cb.current.onChange(latest.current);
      },
      onPanResponderMove: (_, g) => {
        latest.current = fromNorm(startNorm.current + g.dx / width.current);
        cb.current.onChange(latest.current);
      },
      onPanResponderRelease: () => cb.current.onEnd?.(latest.current),
      onPanResponderTerminate: () => cb.current.onEnd?.(latest.current),
    }),
  ).current;
  const n = Math.min(1, Math.max(0, toNorm(value)));
  return (
    <View
      accessible
      accessibilityRole="adjustable"
      accessibilityLabel={label}
      accessibilityValue={{ min: 0, max: 100, now: Math.round(n * 100) }}
      onAccessibilityAction={e => {
        const d = e.nativeEvent.actionName === 'increment' ? 0.05 : -0.05;
        cb.current.onBegin?.();
        const v = fromNorm(n + d);
        cb.current.onChange(v);
        cb.current.onEnd?.(v);
      }}
      accessibilityActions={[{ name: 'increment' }, { name: 'decrement' }]}
      pointerEvents={disabled ? 'none' : 'auto'}
      style={[styles.sliderHit, disabled && { opacity: 0.4 }]}
      onLayout={e => (width.current = Math.max(1, e.nativeEvent.layout.width))}
      {...pan.panHandlers}>
      <View style={styles.sliderTrack}>
        <View style={[styles.sliderFill, { width: `${n * 100}%`, backgroundColor: color }]} />
      </View>
      <View style={[styles.sliderThumb, { left: `${n * 100}%` }]} />
    </View>
  );
}

//------------------------------------------------------------------------------------------ layout

export function Card({ children, style }: { children: React.ReactNode; style?: ViewStyle }) {
  return <View style={[styles.card, style]}>{children}</View>;
}

export function Header({ title, subtitle, onBack, right }: { title: string; subtitle?: string; onBack?: () => void; right?: React.ReactNode }) {
  return (
    <View style={styles.header}>
      {onBack ? <IconButton icon="back" label="Back" size={28} onPress={onBack} /> : <View style={{ width: 8 }} />}
      <View style={{ flex: 1 }}>
        <Text style={styles.headerTitle} numberOfLines={1}>{title}</Text>
        {subtitle ? <Text style={styles.headerSub} numberOfLines={1}>{subtitle}</Text> : null}
      </View>
      {right}
    </View>
  );
}

export function SectionTitle({ children, right }: { children: React.ReactNode; right?: React.ReactNode }) {
  return (
    <View style={[styles.row, { justifyContent: 'space-between', marginTop: space.lg, marginBottom: space.sm }]}>
      <Text style={styles.section}>{children}</Text>
      {right}
    </View>
  );
}

export function Field({ label, value, onChangeText, placeholder, keyboardType, maxLength }: {
  label: string; value: string; onChangeText: (t: string) => void; placeholder?: string; keyboardType?: 'default' | 'numeric'; maxLength?: number;
}) {
  return (
    <View style={{ marginBottom: space.md }}>
      <Text style={styles.label}>{label}</Text>
      <TextInput
        accessibilityLabel={label}
        style={styles.input}
        value={value}
        onChangeText={onChangeText}
        placeholder={placeholder}
        placeholderTextColor={colors.textFaint}
        keyboardType={keyboardType}
        maxLength={maxLength}
      />
    </View>
  );
}

/** Bottom sheet used for focused panels on small screens. */
export function Sheet({ visible, onClose, title, children }: { visible: boolean; onClose: () => void; title: string; children: React.ReactNode }) {
  return (
    <Modal visible={visible} transparent animationType="slide" onRequestClose={onClose}>
      <Pressable style={styles.sheetBackdrop} onPress={onClose} accessibilityLabel="Close" />
      <View style={styles.sheet}>
        <View style={[styles.row, { justifyContent: 'space-between', marginBottom: space.md }]}>
          <Text style={styles.sheetTitle}>{title}</Text>
          <IconButton icon="close" label="Close" onPress={onClose} size={24} />
        </View>
        <ScrollView keyboardShouldPersistTaps="handled">{children}</ScrollView>
      </View>
    </Modal>
  );
}

//------------------------------------------------------------------------------------------ meters & waveform

export function LevelMeter({ l, r, clip, vertical, height = 6 }: { l: number; r: number; clip?: boolean; vertical?: boolean; height?: number }) {
  const toPct = (v: number) => {
    const db = v > 1e-5 ? 20 * Math.log10(v) : -100;
    return Math.max(0, Math.min(1, (db + 60) / 60));
  };
  const bar = (v: number) => {
    const p = toPct(v);
    const c = v >= 1 ? colors.record : v > 0.7 ? colors.amber : colors.green;
    return vertical ? (
      <View style={[styles.meterTrackV]}><View style={{ height: `${p * 100}%`, backgroundColor: c, width: '100%' }} /></View>
    ) : (
      <View style={[styles.meterTrack, { height }]}><View style={{ width: `${p * 100}%`, backgroundColor: c, height: '100%' }} /></View>
    );
  };
  return (
    <View style={vertical ? { flexDirection: 'row', gap: 2, height: '100%' } : { gap: 2 }}>
      {bar(l)}
      {bar(r)}
      {clip ? <View style={styles.clipDot} accessibilityLabel="Clipping" /> : null}
    </View>
  );
}

export function Waveform({ peaks, color, height }: { peaks: number[]; color: string; height: number }) {
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', height, overflow: 'hidden' }}>
      {peaks.map((p, i) => (
        <View key={i} style={{ width: 2, marginRight: 1, height: Math.max(1, Math.min(1, p) * height), backgroundColor: color, borderRadius: 1 }} />
      ))}
    </View>
  );
}

export function Progress({ value, color = colors.cyan }: { value: number; color?: string }) {
  return (
    <View style={styles.progress} accessibilityRole="progressbar" accessibilityValue={{ min: 0, max: 100, now: Math.round(value * 100) }}>
      <View style={{ width: `${Math.max(0, Math.min(1, value)) * 100}%`, height: '100%', backgroundColor: color, borderRadius: 4 }} />
    </View>
  );
}

/** Coloured label for the current effect destination: never colour alone. */
export function DestinationBadge({ kind, name, color }: { kind: 'layer' | 'bus' | 'master' | 'mastering'; name: string; color?: string }) {
  const map = {
    layer: { label: 'LAYER FX', c: color ?? colors.layerFx },
    bus: { label: 'BUS FX', c: colors.busFx },
    master: { label: 'MASTER FX', c: colors.masterFx },
    mastering: { label: 'MASTERING', c: colors.mastering },
  }[kind];
  return (
    <View style={[styles.badge, { borderColor: map.c }]} accessibilityLabel={`${map.label}: ${name}`}>
      <View style={[styles.badgeDot, { backgroundColor: map.c }]} />
      <Text style={styles.badgeKind}>{map.label}</Text>
      <Text style={styles.badgeName} numberOfLines={1}>{name}</Text>
    </View>
  );
}

export function useToast() {
  const [msg, setMsg] = useState<{ text: string; kind: 'info' | 'error' } | null>(null);
  const t = useRef<ReturnType<typeof setTimeout> | null>(null);
  // stable identity: callers use it in effect dependencies
  const show = useCallback((text: string, kind: 'info' | 'error' = 'info') => {
    setMsg({ text, kind });
    if (t.current) clearTimeout(t.current);
    t.current = setTimeout(() => setMsg(null), kind === 'error' ? 5000 : 2500);
  }, []);
  const node = msg ? (
    <View pointerEvents="none" style={[styles.toast, msg.kind === 'error' && { borderColor: colors.danger }]} accessibilityLiveRegion="polite">
      <Text style={styles.toastText}>{msg.text}</Text>
    </View>
  ) : null;
  return { show, node };
}

export const styles = StyleSheet.create({
  row: { flexDirection: 'row', alignItems: 'center', gap: space.sm },
  glow: { shadowColor: colors.purple, shadowOpacity: 0.6, shadowRadius: 12, elevation: 6 },
  button: { minHeight: touch + 4, borderRadius: radius.xl, paddingHorizontal: space.lg, alignItems: 'center', justifyContent: 'center' },
  buttonSmall: { minHeight: 36, paddingHorizontal: space.md, borderRadius: radius.lg },
  buttonText: { color: colors.text, fontSize: font.md, fontWeight: '700' },
  iconButton: { minWidth: touch, minHeight: touch, borderRadius: touch / 2, alignItems: 'center', justifyContent: 'center' },
  chip: { paddingHorizontal: space.md, paddingVertical: 8, borderRadius: radius.lg, borderWidth: 1, borderColor: colors.border, backgroundColor: colors.panel, marginRight: space.sm, marginBottom: space.sm, minHeight: 36, justifyContent: 'center' },
  chipText: { color: colors.textDim, fontSize: font.sm, fontWeight: '600' },
  toggle: { width: 46, height: 26, borderRadius: 13, backgroundColor: colors.border, padding: 3 },
  knob: { width: 20, height: 20, borderRadius: 10, backgroundColor: colors.text },
  sliderHit: { height: 36, justifyContent: 'center' },
  sliderTrack: { height: 4, borderRadius: 2, backgroundColor: colors.border, overflow: 'hidden' },
  sliderFill: { height: '100%' },
  sliderThumb: { position: 'absolute', width: 16, height: 16, marginLeft: -8, borderRadius: 8, backgroundColor: colors.text, top: 10 },
  card: { backgroundColor: colors.panel, borderRadius: radius.lg, padding: space.lg, borderWidth: 1, borderColor: colors.border },
  header: { flexDirection: 'row', alignItems: 'center', paddingHorizontal: space.sm, paddingTop: space.sm, paddingBottom: space.sm, gap: space.sm },
  headerTitle: { color: colors.text, fontSize: font.lg, fontWeight: '700' },
  headerSub: { color: colors.textDim, fontSize: font.xs },
  section: { color: colors.text, fontSize: font.md, fontWeight: '700' },
  label: { color: colors.textDim, fontSize: font.sm, marginBottom: 6 },
  input: { backgroundColor: colors.bgElevated, borderWidth: 1, borderColor: colors.border, borderRadius: radius.md, color: colors.text, paddingHorizontal: space.md, minHeight: touch, fontSize: font.md },
  sheetBackdrop: { flex: 1, backgroundColor: 'rgba(0,0,0,0.55)' },
  sheet: { maxHeight: '85%', backgroundColor: colors.bgElevated, borderTopLeftRadius: radius.xl, borderTopRightRadius: radius.xl, padding: space.lg, borderTopWidth: 1, borderColor: colors.border },
  sheetTitle: { color: colors.text, fontSize: font.lg, fontWeight: '700' },
  meterTrack: { backgroundColor: '#0E1124', borderRadius: 2, overflow: 'hidden' },
  meterTrackV: { width: 5, height: '100%', backgroundColor: '#0E1124', borderRadius: 2, overflow: 'hidden', justifyContent: 'flex-end' },
  clipDot: { position: 'absolute', right: -8, top: 0, width: 6, height: 6, borderRadius: 3, backgroundColor: colors.record },
  progress: { height: 8, borderRadius: 4, backgroundColor: colors.border, overflow: 'hidden' },
  badge: { flexDirection: 'row', alignItems: 'center', gap: 6, borderWidth: 1, borderRadius: radius.lg, paddingHorizontal: space.md, paddingVertical: 6, alignSelf: 'flex-start', maxWidth: '100%' },
  badgeDot: { width: 8, height: 8, borderRadius: 4 },
  badgeKind: { color: colors.text, fontSize: font.xs, fontWeight: '800', letterSpacing: 1 },
  badgeName: { color: colors.textDim, fontSize: font.sm, flexShrink: 1 },
  toast: { position: 'absolute', left: space.lg, right: space.lg, bottom: 90, backgroundColor: colors.panelHigh, borderRadius: radius.md, padding: space.md, borderWidth: 1, borderColor: colors.border },
  toastText: { color: colors.text, fontSize: font.sm },
});
