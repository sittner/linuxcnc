import { reactive, readonly } from 'vue';
import {
  HalscopeClient,
  type ScopeStatus,
  type ThreadInfo,
  type CaptureConfig,
  type TriggerConfig,
  type ChannelConfig,
  ScopeState,
  TrigEdge,
  MAX_CHANNELS,
} from '../generated/halscope_client';
import { HalscopeWatchClient } from '../generated/halscope_watch_client';

// Per-channel UI settings (colors, vertical scale, offset)
export interface ChannelUI {
  color: string;
  vScale: number;   // units per division
  vOffset: number;  // vertical offset in divisions
  visible: boolean;
}

// Decoded sample data per channel
export interface ChannelSamples {
  channel: number;
  data: Float64Array;
}

const CHANNEL_COLORS = [
  '#ffff00', '#00ff00', '#ff4444', '#44aaff',
  '#ff44ff', '#44ffff', '#ff8800', '#88ff00',
  '#ff0088', '#0088ff', '#8800ff', '#00ff88',
  '#ffaa44', '#44ffaa', '#aa44ff', '#ff44aa',
];

interface ScopeStore {
  // Connection
  connected: boolean;
  error: string;

  // Status from server
  status: ScopeStatus;

  // Threads
  threads: ThreadInfo[];

  // Available pins
  pins: string[];
  pinFilter: string;

  // Capture config
  captureConfig: CaptureConfig;

  // Trigger config
  triggerConfig: TriggerConfig;

  // Channel UI settings
  channelUI: ChannelUI[];

  // Sample data
  samples: ChannelSamples[];
  timeBase: Float64Array; // time axis in seconds

  // UI state
  autoRearm: boolean;
  selectedThread: string;
}

const state = reactive<ScopeStore>({
  connected: false,
  error: '',

  status: {
    state: ScopeState.IDLE,
    samples: 0,
    recLen: 4000,
    preTrig: 0,
    sampleLen: 0,
    channels: [],
  },

  threads: [],
  pins: [],
  pinFilter: '',

  captureConfig: {
    threadName: '',
    recLen: 4000,
    samplePeriodMult: 1,
    preTrig: 0,
  },

  triggerConfig: {
    channel: 0,
    level: 0,
    edge: TrigEdge.RISING,
    force: false,
    autoTrig: true,
  },

  channelUI: Array.from({ length: MAX_CHANNELS }, (_, i) => ({
    color: CHANNEL_COLORS[i % CHANNEL_COLORS.length],
    vScale: 1,
    vOffset: 0,
    visible: true,
  })),

  samples: [],
  timeBase: new Float64Array(0),

  autoRearm: false,
  selectedThread: '',
});

let restClient: HalscopeClient | null = null;
let wsClient: HalscopeWatchClient | null = null;

function getBaseUrl(): string {
  return window.location.origin;
}

function getWsUrl(): string {
  const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${proto}//${window.location.host}/api/v1/watch`;
}

// --- Actions ---

async function connect() {
  try {
    restClient = new HalscopeClient(getBaseUrl());
    wsClient = new HalscopeWatchClient(getWsUrl());

    await wsClient.connect();
    state.connected = true;
    state.error = '';

    // Subscribe to state updates
    wsClient.subscribeWatchState(onStatusUpdate, 100);

    // Subscribe to sample data
    wsClient.subscribeWatchSamples(onSamplesUpdate, 100);

    // Load initial data
    const [threads, status] = await Promise.all([
      restClient.listThreads(),
      restClient.getStatus(),
    ]);
    state.threads = threads;
    onStatusUpdate(status);

    if (threads.length > 0 && !state.selectedThread) {
      state.selectedThread = threads[0].name;
      state.captureConfig.threadName = threads[0].name;
    }
  } catch (e) {
    state.error = `Connection failed: ${e}`;
    state.connected = false;
  }
}

function disconnect() {
  wsClient?.close();
  wsClient = null;
  restClient = null;
  state.connected = false;
}

function onStatusUpdate(status: ScopeStatus) {
  state.status = status;

  // Auto-rearm when capture completes
  if (status.state === ScopeState.DONE && state.autoRearm) {
    arm();
  }
}

function onSamplesUpdate(raw: number[]) {
  if (raw.length === 0) return;

  // Raw data: header (4 u32) + sample_count × sample_len × 8 bytes (f64)
  // But the WS transport delivers JSON arrays of u8 bytes.
  // Convert to ArrayBuffer for binary parsing.
  const buf = new Uint8Array(raw).buffer;
  const view = new DataView(buf);

  if (buf.byteLength < 16) return;

  const sampleCount = view.getUint32(0, true);
  const sampleLen = view.getUint32(4, true);
  const startOffset = view.getUint32(8, true);

  if (sampleLen === 0 || sampleCount === 0) return;

  const dataOffset = 16; // header size
  const channels = state.status.channels.filter(c => c.enabled);

  const decoded: ChannelSamples[] = [];
  for (let ci = 0; ci < Math.min(channels.length, sampleLen); ci++) {
    const data = new Float64Array(sampleCount);
    for (let si = 0; si < sampleCount; si++) {
      const byteIdx = dataOffset + (si * sampleLen + ci) * 8;
      if (byteIdx + 8 <= buf.byteLength) {
        data[si] = view.getFloat64(byteIdx, true);
      }
    }
    decoded.push({ channel: channels[ci].channel, data });
  }

  state.samples = decoded;

  // Build time base
  const threadPeriodNs = getSelectedThreadPeriod();
  const dt = (threadPeriodNs * state.captureConfig.samplePeriodMult) / 1e9;
  const tb = new Float64Array(sampleCount);
  const t0 = -(startOffset * dt);
  for (let i = 0; i < sampleCount; i++) {
    tb[i] = t0 + i * dt;
  }
  state.timeBase = tb;
}

function getSelectedThreadPeriod(): number {
  const t = state.threads.find(t => t.name === state.captureConfig.threadName);
  return t?.periodNs ?? 1000000;
}

async function configure() {
  if (!restClient) return;
  try {
    state.captureConfig.threadName = state.selectedThread;
    await restClient.configure(state.captureConfig);
    state.error = '';
  } catch (e) {
    state.error = `Configure failed: ${e}`;
  }
}

async function addChannel(pinName: string, channel: number) {
  if (!restClient) return;
  try {
    const ch: ChannelConfig = { channel, pinName };
    await restClient.setChannel(ch);
    state.error = '';
  } catch (e) {
    state.error = `Set channel failed: ${e}`;
  }
}

async function removeChannel(channel: number) {
  if (!restClient) return;
  try {
    await restClient.clearChannel(channel);
    state.error = '';
  } catch (e) {
    state.error = `Clear channel failed: ${e}`;
  }
}

async function setTrigger() {
  if (!restClient) return;
  try {
    await restClient.setTrigger(state.triggerConfig);
    state.error = '';
  } catch (e) {
    state.error = `Set trigger failed: ${e}`;
  }
}

async function arm() {
  if (!restClient) return;
  try {
    await restClient.arm();
    state.error = '';
  } catch (e) {
    state.error = `Arm failed: ${e}`;
  }
}

async function stop() {
  if (!restClient) return;
  try {
    state.autoRearm = false;
    await restClient.reset();
    state.error = '';
  } catch (e) {
    state.error = `Reset failed: ${e}`;
  }
}

async function searchPins(pattern?: string) {
  if (!restClient) return;
  try {
    state.pins = await restClient.listPins(pattern || undefined);
    state.error = '';
  } catch (e) {
    state.error = `List pins failed: ${e}`;
  }
}

function setAutoRearm(enabled: boolean) {
  state.autoRearm = enabled;
}

// --- Exported store ---

export const scopeStore = {
  state: readonly(state) as ScopeStore,
  connect,
  disconnect,
  configure,
  addChannel,
  removeChannel,
  setTrigger,
  arm,
  stop,
  searchPins,
  setAutoRearm,

  // Mutable config refs for v-model binding
  captureConfig: state.captureConfig,
  triggerConfig: state.triggerConfig,
  channelUI: state.channelUI,

  // Direct state mutation helpers
  setSelectedThread(name: string) {
    state.selectedThread = name;
  },
  setPinFilter(f: string) {
    state.pinFilter = f;
  },
};
