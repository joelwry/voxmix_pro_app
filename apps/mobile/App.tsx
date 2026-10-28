/**
 * VoxMix Pro: mobile vocal studio.
 * All audio runs in the native C++ engine; this layer renders state and sends commands.
 */
import React, { useEffect, useState } from 'react';
import { StatusBar, StyleSheet, View } from 'react-native';
import { SafeAreaProvider, SafeAreaView } from 'react-native-safe-area-context';
import { cmd } from './src/engine/engine';
import { AppProvider, Route, useApp } from './src/state/AppState';
import { EffectEditorScreen, EffectsScreen } from './src/screens/EffectsScreen';
import { AppSettingsScreen, HomeScreen, LibraryScreen, WelcomeScreen } from './src/screens/HomeScreens';
import { ExportScreen, LayersScreen, MasteringScreen, ProjectSettingsScreen } from './src/screens/MixScreens';
import { StudioScreen } from './src/screens/StudioScreen';
import { colors } from './src/ui/theme';

function Router() {
  const { route } = useApp();
  switch (route.name) {
    case 'welcome': return <WelcomeScreen />;
    case 'home': return <HomeScreen />;
    case 'library': return <LibraryScreen />;
    case 'appSettings': return <AppSettingsScreen />;
    case 'studio': return <StudioScreen />;
    case 'layers': return <LayersScreen />;
    case 'effects': return <EffectsScreen key={route.target} target={route.target} />;
    case 'effectEditor': return <EffectEditorScreen key={route.slotId} target={route.target} slotId={route.slotId} />;
    case 'mastering': return <MasteringScreen />;
    case 'export': return <ExportScreen />;
    case 'projectSettings': return <ProjectSettingsScreen />;
  }
}

export default function App() {
  const [initial, setInitial] = useState<Route | null>(null);
  useEffect(() => {
    cmd<any>('getSettings')
      .then(r => setInitial(r.settings?.onboarded ? { name: 'home' } : { name: 'welcome' }))
      .catch(() => setInitial({ name: 'welcome' }));
  }, []);
  return (
    <SafeAreaProvider>
      <StatusBar barStyle="light-content" />
      <SafeAreaView style={styles.root} edges={['top', 'bottom', 'left', 'right']}>
        {initial ? (
          <AppProvider initial={initial}>
            <Router />
          </AppProvider>
        ) : <View style={styles.root} />}
      </SafeAreaView>
    </SafeAreaProvider>
  );
}

const styles = StyleSheet.create({ root: { flex: 1, backgroundColor: colors.bg } });
