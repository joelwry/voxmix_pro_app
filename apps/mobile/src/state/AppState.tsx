// Application state: the open project (always the engine-confirmed copy), effect catalogue,
// a small stack navigator with Android back handling, and error/toast reporting.

import React, { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState } from 'react';
import { BackHandler } from 'react-native';
import { cmd, Reply } from '../engine/engine';
import type { ChainPreset, EffectDescriptor, FxTarget, Project } from '../engine/types';
import { useToast } from '../ui/components';

export type Route =
  | { name: 'welcome' }
  | { name: 'home' }
  | { name: 'library' }
  | { name: 'appSettings' }
  | { name: 'studio' }
  | { name: 'layers' }
  | { name: 'effects'; target: FxTarget }
  | { name: 'effectEditor'; target: FxTarget; slotId: string }
  | { name: 'mastering' }
  | { name: 'export' }
  | { name: 'projectSettings' };

interface AppCtx {
  route: Route;
  navigate: (r: Route) => void;
  replace: (r: Route) => void;
  back: () => void;
  project: Project | null;
  canUndo: boolean;
  canRedo: boolean;
  /** Runs an engine command, applies the returned project, reports errors. Returns null on error. */
  run: <T extends Reply = Reply>(method: string, args?: object, opts?: { quiet?: boolean; success?: string }) => Promise<T | null>;
  setProject: (p: Project | null) => void;
  catalog: EffectDescriptor[];
  describe: (type: string) => EffectDescriptor | undefined;
  presets: ChainPreset[];
  reloadPresets: () => Promise<void>;
  toast: (text: string, kind?: 'info' | 'error') => void;
  selectedTrackId: string | null;
  selectTrack: (id: string | null) => void;
}

const Ctx = createContext<AppCtx | null>(null);

export function useApp(): AppCtx {
  const c = useContext(Ctx);
  if (!c) throw new Error('useApp outside provider');
  return c;
}

export function AppProvider({ children, initial }: { children: React.ReactNode; initial: Route }) {
  const [stack, setStack] = useState<Route[]>([initial]);
  const [project, setProject] = useState<Project | null>(null);
  const [undo, setUndo] = useState({ canUndo: false, canRedo: false });
  const [catalog, setCatalog] = useState<EffectDescriptor[]>([]);
  const [presets, setPresets] = useState<ChainPreset[]>([]);
  const [selectedTrackId, selectTrack] = useState<string | null>(null);
  const { show, node } = useToast();
  const stackRef = useRef(stack);
  stackRef.current = stack;

  const navigate = useCallback((r: Route) => setStack(s => [...s, r]), []);
  const replace = useCallback((r: Route) => setStack(s => [...s.slice(0, -1), r]), []);
  const back = useCallback(() => setStack(s => (s.length > 1 ? s.slice(0, -1) : s)), []);

  useEffect(() => {
    const sub = BackHandler.addEventListener('hardwareBackPress', () => {
      if (stackRef.current.length > 1) {
        back();
        return true;
      }
      return false; // let Android leave the app
    });
    return () => sub.remove();
  }, [back]);

  const run = useCallback(async <T extends Reply = Reply>(method: string, args: object = {}, opts: { quiet?: boolean; success?: string } = {}) => {
    try {
      const r = await cmd<T>(method, args);
      if (r.project) setProject(r.project);
      if (r.canUndo !== undefined) setUndo({ canUndo: !!r.canUndo, canRedo: !!r.canRedo });
      if (Array.isArray(r.warnings)) r.warnings.forEach((w: string) => show(w, 'error'));
      if (typeof r.warning === 'string') show(r.warning, 'error');
      if (opts.success) show(opts.success);
      return r;
    } catch (e: any) {
      if (!opts.quiet) show(e?.message ?? String(e), 'error');
      return null;
    }
  }, [show]);

  const reloadPresets = useCallback(async () => {
    const r = await run<Reply & { presets: ChainPreset[] }>('listPresets', {}, { quiet: true });
    if (r) setPresets(r.presets);
  }, [run]);

  useEffect(() => {
    run<Reply & { effects: EffectDescriptor[] }>('getEffectCatalog', { includeMastering: true }, { quiet: true }).then(r => r && setCatalog(r.effects));
    reloadPresets();
  }, [run, reloadPresets]);

  const describe = useCallback((type: string) => catalog.find(d => d.type === type), [catalog]);

  const value = useMemo<AppCtx>(() => ({
    route: stack[stack.length - 1], navigate, replace, back, project, setProject,
    canUndo: undo.canUndo, canRedo: undo.canRedo, run, catalog, describe, presets, reloadPresets, toast: show,
    selectedTrackId, selectTrack,
  }), [stack, navigate, replace, back, project, undo, run, catalog, describe, presets, reloadPresets, show, selectedTrackId]);

  return (
    <Ctx.Provider value={value}>
      {children}
      {node}
    </Ctx.Provider>
  );
}

/** Human-readable name of an effect destination. */
export function targetLabel(project: Project | null, target: FxTarget): { kind: 'layer' | 'bus' | 'master' | 'mastering'; name: string; color?: string } {
  if (target === 'master') return { kind: 'master', name: 'Whole mix (beat + all vocals)' };
  if (target === 'mastering') return { kind: 'mastering', name: 'Final master' };
  if (target.startsWith('bus:')) {
    const b = project?.buses.find(x => x.id === target.slice(4));
    return { kind: 'bus', name: b?.name ?? 'Bus' };
  }
  const t = project?.tracks.find(x => x.id === target.slice(6));
  return { kind: 'layer', name: t?.name ?? 'Layer', color: t?.color };
}

export function chainFor(project: Project | null, target: FxTarget) {
  if (!project) return [];
  if (target === 'master') return project.master.fx;
  if (target === 'mastering') return [project.master.mastering];
  if (target.startsWith('bus:')) return project.buses.find(b => b.id === target.slice(4))?.fx ?? [];
  return project.tracks.find(t => t.id === target.slice(6))?.fx ?? [];
}
