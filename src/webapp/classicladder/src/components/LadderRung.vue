<script setup lang="ts">
import { computed } from 'vue';
import type { Rung, Element } from '../generated/classicladder_client';

const props = defineProps<{
  rung: Rung;
  rungIndex: number;
  symbols?: Map<string, string>; // varKey -> symbol name
}>();

const emit = defineEmits<{
  (e: 'cellClick', row: number, col: number): void;
}>();

const COLS = 10;
const ROWS = 6;

// Element type constants
const ELE_FREE = 0;
const ELE_INPUT = 1;
const ELE_INPUT_NOT = 2;
const ELE_RISING_INPUT = 3;
const ELE_FALLING_INPUT = 4;
const ELE_CONNECTION = 9;
const ELE_TIMER = 10;
const ELE_MONOSTABLE = 11;
const ELE_COUNTER = 12;
const ELE_TIMER_IEC = 13;
const ELE_COMPAR = 20;
const ELE_OUTPUT = 50;
const ELE_OUTPUT_NOT = 51;
const ELE_OUTPUT_SET = 52;
const ELE_OUTPUT_RESET = 53;
const ELE_OUTPUT_JUMP = 54;
const ELE_OUTPUT_CALL = 55;
const ELE_OUTPUT_OPERATE = 60;

// Variable type constants
const VAR_MEM_BIT = 0;
const VAR_TIMER_DONE = 10;
const VAR_TIMER_RUNNING = 11;
const VAR_TIMER_IEC_DONE = 15;
const VAR_MONOSTABLE_RUNNING = 20;
const VAR_COUNTER_DONE = 25;
const VAR_COUNTER_EMPTY = 26;
const VAR_COUNTER_FULL = 27;
const VAR_STEP_ACTIVITY = 30;
const VAR_PHYS_INPUT = 50;
const VAR_PHYS_OUTPUT = 60;
const VAR_ERROR_BIT = 70;

function getElement(row: number, col: number): Element {
  const idx = row * COLS + col;
  if (props.rung.elements && idx < props.rung.elements.length) {
    return props.rung.elements[idx];
  }
  return { type: 0, connectedWithTop: 0, varType: 0, varNum: 0 };
}

function varPrefix(varType: number): string {
  switch (varType) {
    case VAR_MEM_BIT: return '%B';
    case VAR_TIMER_DONE: return '%TM.D';
    case VAR_TIMER_RUNNING: return '%TM.R';
    case VAR_TIMER_IEC_DONE: return '%TI.D';
    case VAR_MONOSTABLE_RUNNING: return '%M.R';
    case VAR_COUNTER_DONE: return '%C.D';
    case VAR_COUNTER_EMPTY: return '%C.E';
    case VAR_COUNTER_FULL: return '%C.F';
    case VAR_STEP_ACTIVITY: return '%X';
    case VAR_PHYS_INPUT: return '%I';
    case VAR_PHYS_OUTPUT: return '%Q';
    case VAR_ERROR_BIT: return '%E';
    default: return '%?';
  }
}

function varLabel(el: Element): string {
  if (el.type === ELE_FREE || el.type === ELE_CONNECTION) return '';
  const key = `${el.varType}:${el.varNum}`;
  if (props.symbols?.has(key)) return props.symbols.get(key)!;
  return `${varPrefix(el.varType)}${el.varNum}`;
}

function cellClass(el: Element): string {
  const classes = ['cell'];
  if (el.connectedWithTop) classes.push('conn-top');
  switch (el.type) {
    case ELE_FREE: classes.push('free'); break;
    case ELE_INPUT: classes.push('contact open'); break;
    case ELE_INPUT_NOT: classes.push('contact closed'); break;
    case ELE_RISING_INPUT: classes.push('contact rising'); break;
    case ELE_FALLING_INPUT: classes.push('contact falling'); break;
    case ELE_CONNECTION: classes.push('connection'); break;
    case ELE_TIMER: case ELE_MONOSTABLE: case ELE_COUNTER: case ELE_TIMER_IEC:
      classes.push('block'); break;
    case ELE_COMPAR: classes.push('block compare'); break;
    case ELE_OUTPUT: classes.push('coil normal'); break;
    case ELE_OUTPUT_NOT: classes.push('coil negated'); break;
    case ELE_OUTPUT_SET: classes.push('coil set'); break;
    case ELE_OUTPUT_RESET: classes.push('coil reset'); break;
    case ELE_OUTPUT_JUMP: classes.push('coil jump'); break;
    case ELE_OUTPUT_CALL: classes.push('coil call'); break;
    case ELE_OUTPUT_OPERATE: classes.push('block operate'); break;
  }
  return classes.join(' ');
}

function cellSymbol(el: Element): string {
  switch (el.type) {
    case ELE_INPUT: return '| |';
    case ELE_INPUT_NOT: return '|/|';
    case ELE_RISING_INPUT: return '|P|';
    case ELE_FALLING_INPUT: return '|N|';
    case ELE_OUTPUT: return '( )';
    case ELE_OUTPUT_NOT: return '(/)';
    case ELE_OUTPUT_SET: return '(S)';
    case ELE_OUTPUT_RESET: return '(R)';
    case ELE_OUTPUT_JUMP: return '>>>';
    case ELE_OUTPUT_CALL: return 'CAL';
    case ELE_CONNECTION: return '───';
    case ELE_TIMER: return 'TMR';
    case ELE_MONOSTABLE: return 'MON';
    case ELE_COUNTER: return 'CTR';
    case ELE_TIMER_IEC: return 'IEC';
    case ELE_COMPAR: return 'CMP';
    case ELE_OUTPUT_OPERATE: return 'OPR';
    default: return '';
  }
}

const rows = computed(() => {
  const result: { row: number; cells: { col: number; el: Element }[] }[] = [];
  for (let r = 0; r < ROWS; r++) {
    const cells: { col: number; el: Element }[] = [];
    for (let c = 0; c < COLS; c++) {
      cells.push({ col: c, el: getElement(r, c) });
    }
    result.push({ row: r, cells });
  }
  return result;
});
</script>

<template>
  <div class="rung">
    <div class="rung-header">
      <span class="rung-num">{{ rungIndex }}</span>
      <span class="rung-label" v-if="rung.label">{{ rung.label }}</span>
      <span class="rung-comment" v-if="rung.comment">{{ rung.comment }}</span>
    </div>
    <div class="rung-grid">
      <div class="power-rail left"></div>
      <div class="grid-body">
        <div v-for="r in rows" :key="r.row" class="grid-row">
          <div
            v-for="cell in r.cells"
            :key="cell.col"
            :class="cellClass(cell.el)"
            @click="emit('cellClick', r.row, cell.col)"
          >
            <div class="conn-top-line" v-if="cell.el.connectedWithTop"></div>
            <span class="symbol">{{ cellSymbol(cell.el) }}</span>
            <span class="var-label" v-if="varLabel(cell.el)">{{ varLabel(cell.el) }}</span>
          </div>
        </div>
      </div>
      <div class="power-rail right"></div>
    </div>
  </div>
</template>

<style scoped>
.rung {
  margin-bottom: 8px;
  border: 1px solid #45475a;
  border-radius: 4px;
  overflow: hidden;
}

.rung-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 8px;
  background: #1e1e2e;
  border-bottom: 1px solid #45475a;
  font-size: 11px;
}

.rung-num {
  font-weight: 700;
  color: #89b4fa;
  min-width: 24px;
}

.rung-label {
  color: #a6e3a1;
  font-weight: 600;
}

.rung-comment {
  color: #a6adc8;
  font-style: italic;
}

.rung-grid {
  display: flex;
  background: #181825;
}

.power-rail {
  width: 4px;
  background: #89b4fa;
}

.grid-body {
  flex: 1;
  display: flex;
  flex-direction: column;
}

.grid-row {
  display: grid;
  grid-template-columns: repeat(10, 1fr);
  min-height: 36px;
}

.cell {
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  border-right: 1px solid #31324422;
  border-bottom: 1px solid #31324422;
  padding: 2px;
  cursor: pointer;
  min-height: 36px;
  transition: background 0.1s;
}

.cell:hover {
  background: #31324488;
}

.cell.free {
  opacity: 0.3;
}

.cell.connection .symbol {
  color: #585b70;
  font-size: 10px;
}

.cell.contact .symbol,
.cell.coil .symbol {
  font-family: monospace;
  font-size: 13px;
  font-weight: 700;
}

.cell.contact .symbol {
  color: #a6e3a1;
}

.cell.coil .symbol {
  color: #f9e2af;
}

.cell.block .symbol {
  color: #cba6f7;
  font-size: 10px;
  font-weight: 700;
  background: #45475a;
  border-radius: 3px;
  padding: 1px 4px;
}

.var-label {
  font-size: 9px;
  color: #a6adc8;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 100%;
}

.conn-top-line {
  position: absolute;
  top: 0;
  left: 50%;
  width: 2px;
  height: 8px;
  background: #585b70;
  transform: translateX(-50%);
}
</style>
