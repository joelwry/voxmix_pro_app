// Welcome / onboarding, Home (projects), Library (exported songs), App settings.

import React, { useCallback, useEffect, useState } from 'react';
import { Alert, FlatList, Image, Pressable, ScrollView, StyleSheet, Text, View } from 'react-native';
import { formatBytes, formatTime, getDeviceInfo, pickAudioFile, Reply, saveFileAs, shareFile } from '../engine/engine';
import type { DeviceInfo, ProjectSummary } from '../engine/types';
import { useApp } from '../state/AppState';
import { Button, Card, Field, Header, Icon, IconButton, SectionTitle, Sheet } from '../ui/components';
import { colors, font, radius, space } from '../ui/theme';

export function WelcomeScreen() {
  const { replace, run } = useApp();
  const features = [
    ['Multi-layer recording', 'Up to 10 vocal layers, in sync with your beat'],
    ['Real vocal processing', 'Pitch correction, EQ, compression, de-essing, reverb, delay and 40 creative effects'],
    ['Layer, bus & master FX', 'Process one voice, a group, or the whole song'],
    ['Master & export', 'Loudness-matched mastering and validated WAV export'],
  ];
  return (
    <ScrollView contentContainerStyle={s.welcome}>
      <Image source={require('../assets/logo.png')} style={s.logoImage} accessibilityIgnoresInvertColors />
      <Text style={s.logo}>VoxMix <Text style={{ color: colors.purple }}>Pro</Text></Text>
      <Text style={s.tagline}>RECORD · MIX · MASTER</Text>
      <Text style={s.sub}>Your studio. In your pocket.</Text>
      <View style={{ marginTop: space.xl, gap: space.md }}>
        {features.map(([t, d]) => (
          <View key={t} style={s.feature}>
            <View style={s.featureIcon}><Icon name="wave" size={18} color={colors.cyan} /></View>
            <View style={{ flex: 1 }}>
              <Text style={s.featureTitle}>{t}</Text>
              <Text style={s.featureDesc}>{d}</Text>
            </View>
          </View>
        ))}
      </View>
      <Text style={s.note}>
        For the tightest timing, record with wired headphones or a USB audio interface. Bluetooth headphones add a delay
        that no phone can fully remove.
      </Text>
      <Button
        title="Get Started"
        style={{ marginTop: space.xl }}
        onPress={async () => {
          await run('setSettings', { values: { onboarded: true } }, { quiet: true });
          replace({ name: 'home' });
        }}
      />
    </ScrollView>
  );
}

function TabBar({ active }: { active: 'home' | 'library' | 'appSettings' }) {
  const { replace } = useApp();
  const tabs: Array<[typeof active, string, string]> = [['home', 'Home', 'home'], ['library', 'Library', 'library'], ['appSettings', 'Settings', 'gear']];
  return (
    <View style={s.tabbar}>
      {tabs.map(([id, label, icon]) => (
        <Pressable key={id} accessibilityRole="tab" accessibilityState={{ selected: active === id }} style={s.tab} onPress={() => replace({ name: id } as any)}>
          <Icon name={icon} size={20} color={active === id ? colors.purple : colors.textFaint} />
          <Text style={[s.tabText, active === id && { color: colors.purple }]}>{label}</Text>
        </Pressable>
      ))}
    </View>
  );
}

export function HomeScreen() {
  const { run, navigate, toast } = useApp();
  const [projects, setProjects] = useState<ProjectSummary[] | null>(null);
  const [creating, setCreating] = useState(false);
  const [name, setName] = useState('');
  const [bpm, setBpm] = useState('120');
  const [busy, setBusy] = useState(false);
  const [menu, setMenu] = useState<ProjectSummary | null>(null);
  const [rename, setRename] = useState('');

  const load = useCallback(async () => {
    const r = await run<Reply & { projects: ProjectSummary[] }>('listProjects', {}, { quiet: true });
    setProjects(r?.projects ?? []);
  }, [run]);
  useEffect(() => { load(); }, [load]);

  const open = async (id: string) => {
    setBusy(true);
    const r = await run('openProject', { id });
    setBusy(false);
    if (r) navigate({ name: 'studio' });
  };

  const create = async (withBeat: boolean) => {
    setBusy(true);
    let info: DeviceInfo | null = null;
    try { info = await getDeviceInfo(); } catch { /* use default rate */ }
    const rate = info?.nativeSampleRate === 44100 ? 44100 : 48000;
    const r = await run('createProject', { name: name.trim() || 'Untitled Project', bpm: Number(bpm) || 120, sampleRate: rate });
    if (r && withBeat) {
      try {
        const f = await pickAudioFile();
        if (f) await run('importAudio', { path: f.path, name: f.name }, { success: `Imported ${f.name}` });
      } catch (e: any) {
        toast(e?.message ?? 'Import failed', 'error');
      }
    }
    setBusy(false);
    setCreating(false);
    setName('');
    if (r) navigate({ name: 'studio' });
  };

  return (
    <View style={{ flex: 1 }}>
      <View style={[s.homeHeader]}>
        <View style={{ flexDirection: 'row', alignItems: 'center', gap: space.md }}>
          <Image source={require('../assets/logo.png')} style={{ width: 40, height: 40, borderRadius: 10 }} />
          <Text style={s.brand}>VoxMix <Text style={{ color: colors.purple }}>Pro</Text></Text>
        </View>
      </View>
      <FlatList
        data={projects ?? []}
        keyExtractor={p => p.id}
        contentContainerStyle={{ padding: space.lg, paddingBottom: 100 }}
        ListHeaderComponent={
          <>
            <Pressable accessibilityRole="button" onPress={() => setCreating(true)} style={s.newCard}>
              <View style={{ flex: 1 }}>
                <Text style={s.newTitle}>New Project</Text>
                <Text style={s.newSub}>Import a beat and start recording</Text>
              </View>
              <View style={s.plus}><Icon name="plus" size={28} /></View>
            </Pressable>
            <SectionTitle>My Projects</SectionTitle>
            {projects === null ? <Text style={s.dim}>Loading…</Text> : projects.length === 0 ? (
              <Card><Text style={s.dim}>No projects yet. Tap “New Project” to import a beat and record your first vocal.</Text></Card>
            ) : null}
          </>
        }
        renderItem={({ item }) => (
          <Pressable
            accessibilityRole="button"
            accessibilityLabel={`Open ${item.name}`}
            onPress={() => open(item.id)}
            onLongPress={() => { setMenu(item); setRename(item.name); }}
            style={s.projectRow}>
            <View style={s.thumb}><Icon name="wave" size={22} color={colors.purple} /></View>
            <View style={{ flex: 1 }}>
              <Text style={s.projectName} numberOfLines={1}>{item.name}</Text>
              <Text style={s.dim}>{formatTime(item.seconds)} · {item.layers} {item.layers === 1 ? 'layer' : 'layers'}{item.beat ? ` · ${item.beat}` : ''}</Text>
            </View>
            <IconButton icon="more" label={`Options for ${item.name}`} onPress={() => { setMenu(item); setRename(item.name); }} />
          </Pressable>
        )}
      />
      <TabBar active="home" />

      <Sheet visible={creating} onClose={() => setCreating(false)} title="New Project">
        <Field label="Project name" value={name} onChangeText={setName} placeholder="Untitled Project" maxLength={60} />
        <Field label="Tempo (BPM)" value={bpm} onChangeText={t => setBpm(t.replace(/[^0-9.]/g, ''))} keyboardType="numeric" maxLength={5} />
        <Button title="Create & import beat" icon="plus" busy={busy} onPress={() => create(true)} />
        <Button title="Create without a beat" variant="ghost" style={{ marginTop: space.sm }} disabled={busy} onPress={() => create(false)} />
      </Sheet>

      <Sheet visible={!!menu} onClose={() => setMenu(null)} title={menu?.name ?? ''}>
        <Field label="Name" value={rename} onChangeText={setRename} maxLength={60} />
        <Button
          title="Rename"
          onPress={async () => {
            if (!menu) return;
            if (await run('openProject', { id: menu.id }, {})) {
              await run('updateProject', { name: rename.trim() || menu.name });
              await run('closeProject');
            }
            setMenu(null);
            load();
          }}
        />
        <Button
          title="Delete project"
          variant="danger"
          style={{ marginTop: space.sm }}
          onPress={() =>
            Alert.alert('Delete project?', `“${menu?.name}” and all of its recordings will be removed from this phone.`, [
              { text: 'Cancel', style: 'cancel' },
              {
                text: 'Delete', style: 'destructive',
                onPress: async () => {
                  if (menu && (await run('deleteProject', { id: menu.id }, { success: 'Project deleted' }))) { setMenu(null); load(); }
                },
              },
            ])
          }
        />
      </Sheet>
    </View>
  );
}

export function LibraryScreen() {
  const { run, toast } = useApp();
  const [files, setFiles] = useState<Array<{ name: string; path: string; bytes: number; modifiedAt: number }>>([]);
  const load = useCallback(async () => {
    const r = await run<Reply & { exports: typeof files }>('listExports', {}, { quiet: true });
    setFiles(r?.exports ?? []);
  }, [run]);
  useEffect(() => { load(); }, [load]);
  return (
    <View style={{ flex: 1 }}>
      <Header title="Library" subtitle="Your exported songs" />
      <FlatList
        data={files}
        keyExtractor={f => f.path}
        contentContainerStyle={{ padding: space.lg, paddingBottom: 100 }}
        ListEmptyComponent={<Card><Text style={s.dim}>Exported songs appear here. Open a project and tap Export.</Text></Card>}
        renderItem={({ item }) => (
          <View style={s.projectRow}>
            <View style={s.thumb}><Icon name="library" size={20} color={colors.cyan} /></View>
            <View style={{ flex: 1 }}>
              <Text style={s.projectName} numberOfLines={1}>{item.name}</Text>
              <Text style={s.dim}>WAV · {formatBytes(item.bytes)} · {new Date(item.modifiedAt).toLocaleDateString()}</Text>
            </View>
            <IconButton icon="save" label={`Save ${item.name} to device`} onPress={async () => {
              try { const n = await saveFileAs(item.path, `${item.name}.wav`); if (n) toast(`Saved ${n}`); } catch (e: any) { toast(e.message, 'error'); }
            }} />
            <IconButton icon="share" label={`Share ${item.name}`} onPress={() => shareFile(item.path).catch((e: any) => toast(e.message, 'error'))} />
            <IconButton icon="trash" label={`Delete ${item.name}`} onPress={() => Alert.alert('Delete export?', item.name, [
              { text: 'Cancel', style: 'cancel' },
              { text: 'Delete', style: 'destructive', onPress: async () => { await run('deleteExport', { path: item.path }); load(); } },
            ])} />
          </View>
        )}
      />
      <TabBar active="library" />
    </View>
  );
}

export function AppSettingsScreen() {
  const { run } = useApp();
  const [info, setInfo] = useState<DeviceInfo | null>(null);
  const [engine, setEngine] = useState<any>(null);
  useEffect(() => {
    getDeviceInfo().then(setInfo).catch(() => setInfo(null));
    run('getInfo', {}, { quiet: true }).then(setEngine);
  }, [run]);
  const row = (k: string, v: string) => (
    <View style={s.kv} key={k}><Text style={s.dim}>{k}</Text><Text style={s.kvValue}>{v}</Text></View>
  );
  return (
    <View style={{ flex: 1 }}>
      <Header title="Settings" />
      <ScrollView contentContainerStyle={{ padding: space.lg, paddingBottom: 100 }}>
        <SectionTitle>Audio hardware</SectionTitle>
        <Card>
          {info ? (
            <>
              {row('Device', info.model)}
              {row('Android API', String(info.sdk))}
              {row('Native sample rate', `${info.nativeSampleRate} Hz`)}
              {row('Native buffer', `${info.framesPerBuffer} frames`)}
              {row('Low-latency audio', info.lowLatency ? 'Yes' : 'No')}
              {row('Pro audio', info.proAudio ? 'Yes' : 'No')}
              {row('Wired headphones', info.wiredHeadphones ? 'Connected' : 'Not connected')}
              {row('USB audio', info.usbAudio ? 'Connected' : 'Not connected')}
              {row('Bluetooth output', info.bluetooth ? 'Connected (adds latency)' : 'Not connected')}
            </>
          ) : <Text style={s.dim}>Reading device…</Text>}
        </Card>
        <Text style={[s.dim, { marginTop: space.sm }]}>
          Open a project and use Project Settings → “Measure latency” to calibrate recording sync on this phone.
        </Text>
        <SectionTitle>About</SectionTitle>
        <Card>
          {row('App version', '0.1.0 (debug test build)')}
          {row('Engine', engine?.version ?? '…')}
          <Text style={[s.dim, { marginTop: space.sm }]}>
            DSP built on the VocalSweet and MoodShift FX processing code, Google Oboe (Apache-2.0), dr_wav / dr_mp3 (public domain),
            nlohmann/json (MIT) and React Native (MIT). See THIRD_PARTY_NOTICES.md in the source repository.
          </Text>
        </Card>
      </ScrollView>
      <TabBar active="appSettings" />
    </View>
  );
}

const s = StyleSheet.create({
  welcome: { padding: space.xl, paddingTop: 40, flexGrow: 1 },
  logoImage: { width: 112, height: 112, borderRadius: 28, alignSelf: 'center', marginBottom: space.lg },
  logo: { color: colors.text, fontSize: 44, fontWeight: '800', textAlign: 'center' },
  tagline: { color: colors.textDim, letterSpacing: 4, textAlign: 'center', marginTop: space.sm },
  sub: { color: colors.textDim, textAlign: 'center', marginTop: space.sm, fontSize: font.md },
  feature: { flexDirection: 'row', gap: space.md, alignItems: 'center' },
  featureIcon: { width: 40, height: 40, borderRadius: 20, backgroundColor: colors.panelHigh, alignItems: 'center', justifyContent: 'center' },
  featureTitle: { color: colors.text, fontWeight: '700', fontSize: font.md },
  featureDesc: { color: colors.textDim, fontSize: font.sm },
  note: { color: colors.textFaint, fontSize: font.xs, marginTop: space.xl, textAlign: 'center' },
  homeHeader: { paddingHorizontal: space.lg, paddingTop: space.lg },
  brand: { color: colors.text, fontSize: font.xxl, fontWeight: '800' },
  newCard: { flexDirection: 'row', alignItems: 'center', backgroundColor: colors.purpleDeep, borderRadius: radius.lg, padding: space.lg, minHeight: 100 },
  newTitle: { color: colors.text, fontSize: font.xl, fontWeight: '800' },
  newSub: { color: '#DDD6FE', fontSize: font.sm, marginTop: 4 },
  plus: { width: 54, height: 54, borderRadius: 27, backgroundColor: colors.blue, alignItems: 'center', justifyContent: 'center' },
  projectRow: { flexDirection: 'row', alignItems: 'center', gap: space.md, backgroundColor: colors.panel, borderRadius: radius.md, padding: space.md, marginBottom: space.sm, minHeight: 64 },
  thumb: { width: 48, height: 48, borderRadius: radius.sm, backgroundColor: colors.panelHigh, alignItems: 'center', justifyContent: 'center' },
  projectName: { color: colors.text, fontSize: font.md, fontWeight: '600' },
  dim: { color: colors.textDim, fontSize: font.sm },
  tabbar: { position: 'absolute', left: 0, right: 0, bottom: 0, flexDirection: 'row', backgroundColor: colors.bgElevated, borderTopWidth: 1, borderColor: colors.border, paddingBottom: 6 },
  tab: { flex: 1, alignItems: 'center', paddingVertical: 8, minHeight: 56, justifyContent: 'center' },
  tabText: { color: colors.textFaint, fontSize: font.xs, marginTop: 2 },
  kv: { flexDirection: 'row', justifyContent: 'space-between', paddingVertical: 6, gap: space.md },
  kvValue: { color: colors.text, fontSize: font.sm, flexShrink: 1, textAlign: 'right' },
});

