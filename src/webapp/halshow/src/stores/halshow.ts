import { reactive } from 'vue';
import {
  HalcmdClient,
  type PinInfo,
  type ParamInfo,
  type SignalInfo,
  type ComponentInfo,
  type FunctionInfo,
  type ThreadInfo,
  type HalStatus,
  type CmdResult,
} from '../generated/halcmd_client';
import { HalcmdWatchClient } from '../generated/halcmd_watch_client';

// Tree node representing HAL hierarchy
export interface TreeNode {
  name: string;       // short name (last segment)
  fullPath: string;   // full dotted path
  children: TreeNode[];
  isLeaf: boolean;
  kind?: 'pin' | 'param' | 'signal' | 'component' | 'function' | 'thread';
  expanded?: boolean;
}

export type TabId = 'show' | 'watch';

export type TreeCategory = 'pins' | 'params' | 'signals' | 'components' | 'functions' | 'threads';

interface HalshowState {
  // Connection
  connected: boolean;
  error: string;

  // HAL data
  pins: PinInfo[];
  params: ParamInfo[];
  signals: SignalInfo[];
  components: ComponentInfo[];
  functions: FunctionInfo[];
  threads: ThreadInfo[];
  status: HalStatus | null;

  // Tree
  treeCategory: TreeCategory;
  treeFilter: string;
  treeNodes: TreeNode[];
  selectedNode: TreeNode | null;

  // Detail (Show tab)
  selectedItem: PinInfo | ParamInfo | SignalInfo | ComponentInfo | FunctionInfo | ThreadInfo | null;
  selectedItemKind: TreeCategory | null;

  // Watch tab
  watchList: string[];     // names of items being watched
  watchValues: PinInfo[];  // live values from WebSocket
  watchRate: number;       // ms

  // Active tab
  activeTab: TabId;
}

const state = reactive<HalshowState>({
  connected: false,
  error: '',

  pins: [],
  params: [],
  signals: [],
  components: [],
  functions: [],
  threads: [],
  status: null,

  treeCategory: 'pins',
  treeFilter: '',
  treeNodes: [],
  selectedNode: null,

  selectedItem: null,
  selectedItemKind: null,

  watchList: [],
  watchValues: [],
  watchRate: 100,

  activeTab: 'show',
});

let client: HalcmdClient;
let watchClient: HalcmdWatchClient;

const WATCH_STORAGE_KEY = 'halshow-watch-list';

function saveWatchList(names: string[]) {
  try {
    localStorage.setItem(WATCH_STORAGE_KEY, JSON.stringify(names));
  } catch { /* quota or private mode — ignore */ }
}

function loadWatchList(): string[] {
  try {
    const raw = localStorage.getItem(WATCH_STORAGE_KEY);
    if (raw) {
      const arr = JSON.parse(raw);
      if (Array.isArray(arr)) return arr.filter((s): s is string => typeof s === 'string');
    }
  } catch { /* corrupt data — ignore */ }
  return [];
}

function buildTree(items: { name: string }[], kind: TreeCategory): TreeNode[] {
  const root: TreeNode[] = [];
  const map = new Map<string, TreeNode>();

  const leafKind = kind === 'pins' ? 'pin' : kind === 'params' ? 'param'
    : kind === 'signals' ? 'signal' : kind === 'components' ? 'component'
    : kind === 'functions' ? 'function' : 'thread';

  for (const item of items) {
    const parts = item.name.split('.');
    let parent = root;
    let path = '';

    for (let i = 0; i < parts.length; i++) {
      const segment = parts[i];
      path = path ? path + '.' + segment : segment;
      const isLeaf = i === parts.length - 1;

      let node = map.get(path);
      if (!node) {
        node = {
          name: segment,
          fullPath: path,
          children: [],
          isLeaf,
          kind: isLeaf ? leafKind : undefined,
          expanded: false,
        };
        map.set(path, node);
        parent.push(node);
      }
      parent = node.children;
    }
  }

  return root;
}

function filterTree(nodes: TreeNode[], filter: string): TreeNode[] {
  if (!filter) return nodes;
  const lower = filter.toLowerCase();
  const result: TreeNode[] = [];
  for (const node of nodes) {
    if (node.fullPath.toLowerCase().includes(lower)) {
      result.push(node);
    } else if (!node.isLeaf) {
      const filteredChildren = filterTree(node.children, filter);
      if (filteredChildren.length > 0) {
        result.push({ ...node, children: filteredChildren, expanded: true });
      }
    }
  }
  return result;
}

export const halshowStore = {
  state,

  async connect() {
    const origin = window.location.origin;
    client = new HalcmdClient(origin);

    try {
      await this.refresh();
      state.connected = true;
      state.error = '';
    } catch (e) {
      state.error = e instanceof Error ? e.message : String(e);
    }

    // Connect WebSocket for watch
    const wsProto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${wsProto}//${window.location.host}/api/v1/watch`;
    watchClient = new HalcmdWatchClient(wsUrl);
    try {
      await watchClient.connect();
      watchClient.onClose = () => {
        state.connected = false;
      };
      // Restore saved watch list now that WebSocket is ready
      this.restoreWatchList();
    } catch {
      // Watch is optional — REST still works
    }
  },

  async refresh() {
    const [pins, params, signals, components, functions, threads, status] = await Promise.all([
      client.listPins(),
      client.listParams(),
      client.listSignals(),
      client.listComponents(),
      client.listFunctions(),
      client.listThreads(),
      client.getStatus(),
    ]);
    state.pins = pins;
    state.params = params;
    state.signals = signals;
    state.components = components;
    state.functions = functions;
    state.threads = threads;
    state.status = status;
    this.rebuildTree();
  },

  rebuildTree() {
    const items = this.getCategoryItems(state.treeCategory);
    const raw = buildTree(items, state.treeCategory);
    state.treeNodes = filterTree(raw, state.treeFilter);
  },

  getCategoryItems(cat: TreeCategory): { name: string }[] {
    switch (cat) {
      case 'pins': return state.pins;
      case 'params': return state.params;
      case 'signals': return state.signals;
      case 'components': return state.components;
      case 'functions': return state.functions;
      case 'threads': return state.threads;
    }
  },

  setCategory(cat: TreeCategory) {
    state.treeCategory = cat;
    state.selectedNode = null;
    state.selectedItem = null;
    state.selectedItemKind = null;
    this.rebuildTree();
  },

  setFilter(filter: string) {
    state.treeFilter = filter;
    this.rebuildTree();
  },

  async selectNode(node: TreeNode) {
    state.selectedNode = node;
    if (!node.isLeaf) {
      node.expanded = !node.expanded;
      return;
    }

    state.selectedItemKind = state.treeCategory;
    try {
      switch (state.treeCategory) {
        case 'pins':
          state.selectedItem = await client.getPin(node.fullPath);
          break;
        case 'params':
          state.selectedItem = await client.getParam(node.fullPath);
          break;
        case 'signals':
          state.selectedItem = await client.getSignal(node.fullPath);
          break;
        default:
          // For components/functions/threads, find from local data
          state.selectedItem = this.getCategoryItems(state.treeCategory)
            .find(i => i.name === node.fullPath) as typeof state.selectedItem;
      }
    } catch (e) {
      state.error = e instanceof Error ? e.message : String(e);
    }
  },

  // --- Watch Tab ---

  addToWatch(name: string) {
    if (!state.watchList.includes(name)) {
      state.watchList.push(name);
      saveWatchList(state.watchList);
      this.updateWatch();
    }
  },

  removeFromWatch(name: string) {
    const idx = state.watchList.indexOf(name);
    if (idx >= 0) {
      state.watchList.splice(idx, 1);
      saveWatchList(state.watchList);
      this.updateWatch();
    }
  },

  clearWatch() {
    state.watchList = [];
    state.watchValues = [];
    saveWatchList(state.watchList);
    watchClient?.unsubscribeWatchItems();
  },

  /** Restore watch list from localStorage, dropping pins/params that no longer exist. */
  restoreWatchList() {
    const saved = loadWatchList();
    if (saved.length === 0) return;

    const knownNames = new Set<string>();
    for (const p of state.pins) knownNames.add(p.name);
    for (const p of state.params) knownNames.add(p.name);
    for (const s of state.signals) knownNames.add(s.name);

    const valid = saved.filter(n => knownNames.has(n));
    if (valid.length !== saved.length) {
      saveWatchList(valid); // prune stale entries
    }
    if (valid.length > 0) {
      state.watchList = valid;
      state.activeTab = 'watch';
      this.updateWatch();
    }
  },

  updateWatch() {
    if (state.watchList.length === 0) {
      watchClient?.unsubscribeWatchItems();
      state.watchValues = [];
      return;
    }
    watchClient?.subscribeWatchItems((data) => {
      state.watchValues = data;
    }, state.watchRate);
  },

  // --- Mutations ---

  async setValue(name: string, value: string, kind: 'pin' | 'param' | 'signal'): Promise<CmdResult> {
    let result: CmdResult;
    switch (kind) {
      case 'pin':
        result = await client.setPin(name, value);
        break;
      case 'param':
        result = await client.setParam(name, value);
        break;
      case 'signal':
        result = await client.setSignal(name, value);
        break;
    }
    return result;
  },

  async unlinkPin(name: string): Promise<CmdResult> {
    return await client.unlink(name);
  },

  setActiveTab(tab: TabId) {
    state.activeTab = tab;
  },
};
