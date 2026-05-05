import { ref, watch } from 'vue';
const props = defineProps();
const emit = defineEmits();
const form = ref({
    toolno: 0, pocketno: 0,
    x_offset: 0, y_offset: 0, z_offset: 0,
    a_offset: 0, b_offset: 0, c_offset: 0,
    u_offset: 0, v_offset: 0, w_offset: 0,
    diameter: 0, frontangle: 0, backangle: 0,
    orientation: 0, comment: '',
});
watch(() => props.tool, (t) => {
    if (t) {
        form.value = { ...t };
    }
}, { immediate: true });
const fields = [
    { key: 'toolno', label: 'Tool Number', type: 'int', readonly: false },
    { key: 'pocketno', label: 'Pocket', type: 'int' },
    { key: 'x_offset', label: 'X Offset', type: 'float' },
    { key: 'y_offset', label: 'Y Offset', type: 'float' },
    { key: 'z_offset', label: 'Z Offset', type: 'float' },
    { key: 'a_offset', label: 'A Offset', type: 'float' },
    { key: 'b_offset', label: 'B Offset', type: 'float' },
    { key: 'c_offset', label: 'C Offset', type: 'float' },
    { key: 'u_offset', label: 'U Offset', type: 'float' },
    { key: 'v_offset', label: 'V Offset', type: 'float' },
    { key: 'w_offset', label: 'W Offset', type: 'float' },
    { key: 'diameter', label: 'Diameter', type: 'float' },
    { key: 'frontangle', label: 'Front Angle', type: 'float' },
    { key: 'backangle', label: 'Back Angle', type: 'float' },
    { key: 'orientation', label: 'Orientation', type: 'int' },
    { key: 'comment', label: 'Comment', type: 'text' },
];
function onInput(field, event) {
    const input = event.target;
    if (field.type === 'text') {
        form.value[field.key] = input.value;
    }
    else {
        const val = Number(input.value);
        if (!isNaN(val)) {
            form.value[field.key] = val;
        }
    }
}
const errors = ref([]);
function validate() {
    const e = [];
    const f = form.value;
    if (f.toolno <= 0)
        e.push('Tool number must be > 0');
    if (f.pocketno < 0 || f.pocketno > 1000)
        e.push('Pocket must be 0–1000');
    if (f.orientation < 0 || f.orientation > 9)
        e.push('Orientation must be 0–9');
    if (f.frontangle < -360 || f.frontangle > 360)
        e.push('Front angle must be -360..360');
    if (f.backangle < -360 || f.backangle > 360)
        e.push('Back angle must be -360..360');
    errors.value = e;
    return e.length === 0;
}
function onSave() {
    if (!validate())
        return;
    emit('save', { ...form.value });
}
debugger; /* PartiallyEnd: #3632/scriptSetup.vue */
const __VLS_ctx = {};
let __VLS_components;
let __VLS_directives;
/** @type {__VLS_StyleScopedClasses['dialog-header']} */ ;
/** @type {__VLS_StyleScopedClasses['btn-close']} */ ;
/** @type {__VLS_StyleScopedClasses['field-row']} */ ;
/** @type {__VLS_StyleScopedClasses['field-row']} */ ;
/** @type {__VLS_StyleScopedClasses['field-row']} */ ;
/** @type {__VLS_StyleScopedClasses['field-row']} */ ;
/** @type {__VLS_StyleScopedClasses['field-row']} */ ;
/** @type {__VLS_StyleScopedClasses['btn-save']} */ ;
/** @type {__VLS_StyleScopedClasses['btn-cancel']} */ ;
// CSS variable injection 
// CSS variable injection end 
__VLS_asFunctionalElement(__VLS_intrinsicElements.div, __VLS_intrinsicElements.div)({
    ...{ onClick: (...[$event]) => {
            __VLS_ctx.emit('cancel');
        } },
    ...{ class: "dialog-overlay" },
});
__VLS_asFunctionalElement(__VLS_intrinsicElements.div, __VLS_intrinsicElements.div)({
    ...{ class: "dialog" },
});
__VLS_asFunctionalElement(__VLS_intrinsicElements.div, __VLS_intrinsicElements.div)({
    ...{ class: "dialog-header" },
});
__VLS_asFunctionalElement(__VLS_intrinsicElements.h2, __VLS_intrinsicElements.h2)({});
(__VLS_ctx.isNew ? 'Add Tool' : `Edit Tool ${__VLS_ctx.form.toolno}`);
__VLS_asFunctionalElement(__VLS_intrinsicElements.button, __VLS_intrinsicElements.button)({
    ...{ onClick: (...[$event]) => {
            __VLS_ctx.emit('cancel');
        } },
    ...{ class: "btn-close" },
});
__VLS_asFunctionalElement(__VLS_intrinsicElements.div, __VLS_intrinsicElements.div)({
    ...{ class: "dialog-body" },
});
for (const [field] of __VLS_getVForSourceType((__VLS_ctx.fields))) {
    __VLS_asFunctionalElement(__VLS_intrinsicElements.div, __VLS_intrinsicElements.div)({
        key: (field.key),
        ...{ class: "field-row" },
    });
    __VLS_asFunctionalElement(__VLS_intrinsicElements.label, __VLS_intrinsicElements.label)({
        for: ('f-' + field.key),
    });
    (field.label);
    if (field.type === 'text') {
        __VLS_asFunctionalElement(__VLS_intrinsicElements.input)({
            ...{ onInput: (...[$event]) => {
                    if (!(field.type === 'text'))
                        return;
                    __VLS_ctx.onInput(field, $event);
                } },
            id: ('f-' + field.key),
            type: "text",
            value: (__VLS_ctx.form[field.key]),
        });
    }
    else {
        __VLS_asFunctionalElement(__VLS_intrinsicElements.input)({
            ...{ onInput: (...[$event]) => {
                    if (!!(field.type === 'text'))
                        return;
                    __VLS_ctx.onInput(field, $event);
                } },
            id: ('f-' + field.key),
            type: "number",
            value: (__VLS_ctx.form[field.key]),
            step: (field.type === 'int' ? 1 : 0.0001),
            readonly: (field.readonly && !__VLS_ctx.isNew),
        });
    }
}
if (__VLS_ctx.errors.length) {
    __VLS_asFunctionalElement(__VLS_intrinsicElements.div, __VLS_intrinsicElements.div)({
        ...{ class: "dialog-errors" },
    });
    for (const [err, i] of __VLS_getVForSourceType((__VLS_ctx.errors))) {
        __VLS_asFunctionalElement(__VLS_intrinsicElements.div, __VLS_intrinsicElements.div)({
            key: (i),
        });
        (err);
    }
}
__VLS_asFunctionalElement(__VLS_intrinsicElements.div, __VLS_intrinsicElements.div)({
    ...{ class: "dialog-footer" },
});
__VLS_asFunctionalElement(__VLS_intrinsicElements.button, __VLS_intrinsicElements.button)({
    ...{ onClick: (...[$event]) => {
            __VLS_ctx.emit('cancel');
        } },
    ...{ class: "btn-cancel" },
});
__VLS_asFunctionalElement(__VLS_intrinsicElements.button, __VLS_intrinsicElements.button)({
    ...{ onClick: (__VLS_ctx.onSave) },
    ...{ class: "btn-save" },
});
/** @type {__VLS_StyleScopedClasses['dialog-overlay']} */ ;
/** @type {__VLS_StyleScopedClasses['dialog']} */ ;
/** @type {__VLS_StyleScopedClasses['dialog-header']} */ ;
/** @type {__VLS_StyleScopedClasses['btn-close']} */ ;
/** @type {__VLS_StyleScopedClasses['dialog-body']} */ ;
/** @type {__VLS_StyleScopedClasses['field-row']} */ ;
/** @type {__VLS_StyleScopedClasses['dialog-errors']} */ ;
/** @type {__VLS_StyleScopedClasses['dialog-footer']} */ ;
/** @type {__VLS_StyleScopedClasses['btn-cancel']} */ ;
/** @type {__VLS_StyleScopedClasses['btn-save']} */ ;
var __VLS_dollars;
const __VLS_self = (await import('vue')).defineComponent({
    setup() {
        return {
            emit: emit,
            form: form,
            fields: fields,
            onInput: onInput,
            errors: errors,
            onSave: onSave,
        };
    },
    __typeEmits: {},
    __typeProps: {},
});
export default (await import('vue')).defineComponent({
    setup() {
        return {};
    },
    __typeEmits: {},
    __typeProps: {},
});
; /* PartiallyEnd: #4569/main.vue */
