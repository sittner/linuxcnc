import { reactive } from 'vue';
import {
  ClassicladderClient,
  type Program,
  LadderState,
} from '../generated/classicladder_client';

export interface LadderStoreState {
  program: Program | null;
  activeSection: number;
  editTool: number; // element type to place, 0 = delete
  selectedCell: { rungIdx: number; row: number; col: number } | null;
  dirty: boolean;
  symbolMap: Map<string, string>;
  loading: boolean;
  error: string;
}

const COLS = 10;

const client = new ClassicladderClient(window.location.origin);

const state = reactive<LadderStoreState>({
  program: null,
  activeSection: 0,
  editTool: -1, // -1 = no tool selected
  selectedCell: null,
  dirty: false,
  symbolMap: new Map(),
  loading: false,
  error: '',
});

async function fetchProgram() {
  state.loading = true;
  state.error = '';
  try {
    state.program = await client.getProgram();
    buildSymbolMap();
    state.dirty = false;
    // Set active section to first used section
    if (state.program.sections) {
      const firstUsed = state.program.sections.findIndex(s => s.used);
      if (firstUsed >= 0) state.activeSection = firstUsed;
    }
  } catch (e: unknown) {
    state.error = (e as Error).message;
  } finally {
    state.loading = false;
  }
}

function buildSymbolMap() {
  state.symbolMap.clear();
  if (!state.program?.symbols) return;
  for (const sym of state.program.symbols) {
    if (sym.varName && sym.symbol) {
      // varName is like "%B0", "%I3", etc.
      state.symbolMap.set(sym.varName, sym.symbol);
    }
  }
}

function setActiveSection(index: number) {
  state.activeSection = index;
}

function setEditTool(type: number) {
  state.editTool = type;
}

function selectCell(rungIdx: number, row: number, col: number) {
  state.selectedCell = { rungIdx, row, col };

  // If a tool is selected, place it
  if (state.editTool >= 0 && state.program) {
    const rung = state.program.rungs[rungIdx];
    if (!rung) return;
    const elIdx = row * COLS + col;
    if (elIdx >= rung.elements.length) return;

    const el = rung.elements[elIdx];
    if (state.editTool === 0) {
      // Delete
      el.type = 0;
      el.connectedWithTop = 0;
      el.varType = 0;
      el.varNum = 0;
    } else {
      el.type = state.editTool;
      // Keep existing var binding unless it's a new element from free
    }
    state.dirty = true;
  }
}

function toggleTopConnection() {
  if (!state.selectedCell || !state.program) return;
  const { rungIdx, row, col } = state.selectedCell;
  const rung = state.program.rungs[rungIdx];
  if (!rung) return;
  const elIdx = row * COLS + col;
  if (elIdx >= rung.elements.length) return;
  rung.elements[elIdx].connectedWithTop = rung.elements[elIdx].connectedWithTop ? 0 : 1;
  state.dirty = true;
}

async function saveProgram() {
  if (!state.program || !state.dirty) return;
  state.error = '';
  try {
    await client.setProgram(state.program);
    state.dirty = false;
  } catch (e: unknown) {
    state.error = (e as Error).message;
  }
}

async function setState(s: LadderState) {
  try {
    await client.setState(s);
  } catch (e: unknown) {
    state.error = (e as Error).message;
  }
}

async function setVariable(varType: number, offset: number, value: number) {
  try {
    await client.setVariable(varType, offset, value);
  } catch (e: unknown) {
    state.error = (e as Error).message;
  }
}

export const ladderStore = {
  state,
  fetchProgram,
  setActiveSection,
  setEditTool,
  selectCell,
  toggleTopConnection,
  saveProgram,
  setState,
  setVariable,
};
