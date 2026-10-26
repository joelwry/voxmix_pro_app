/**
 * TurboModule spec for the native VoxMix engine (codegen input, see package.json codegenConfig).
 * Engine commands go through `call` as JSON; audio never crosses the bridge.
 */
import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

export interface Spec extends TurboModule {
  /** Runs an engine command on a background thread; resolves with the JSON reply. */
  call(method: string, argsJson: string): Promise<string>;
  /** Real-time status snapshot (transport, meters). Synchronous and cheap: poll at ~30 Hz. */
  getStatus(): string;
  /** Opens the system file picker for audio; resolves with {"path","name","size"} JSON or "" if cancelled. */
  pickAudioFile(): Promise<string>;
  /** Lets the user choose where to save a copy of a file (system "Save as" dialog). Resolves with the chosen name or "". */
  saveFileAs(path: string, suggestedName: string, mimeType: string): Promise<string>;
  /** Opens the Android share sheet for a file. */
  shareFile(path: string, mimeType: string): Promise<boolean>;
  /** Device audio capabilities (native rate, burst, low-latency / pro-audio feature flags, devices). */
  getAudioDeviceInfo(): Promise<string>;
}

export default TurboModuleRegistry.getEnforcing<Spec>('VoxEngine');
