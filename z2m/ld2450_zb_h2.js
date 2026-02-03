'use strict';

// Zigbee2MQTT external converter for LD2450-ZB-H2 mmWave presence sensor.
// Zero external requires - works with any Z2M version (tested Z2M 2.8.0).
// Place this file in your Z2M data/external_converters/ directory.

// ---- ZCL data type constants (inline to avoid require('zigbee-herdsman')) ----
const ZCL_UINT8    = 0x20;
const ZCL_UINT16   = 0x21;
const ZCL_INT16    = 0x29;
const ZCL_CHAR_STR = 0x42;

// ---- Expose access flags ----
const ACCESS_STATE     = 0b001; // 1
const ACCESS_SET       = 0b010; // 2
const ACCESS_STATE_SET = 0b011; // 3
const ACCESS_ALL       = 0b111; // 7

// ---- Cluster / attribute IDs ----
const CLUSTER_CONFIG_ID = 0xFC00;
const CLUSTER_ZONE_ID   = 0xFC01;

const ZONE_ATTR_NAMES = ['zoneX1','zoneY1','zoneX2','zoneY2','zoneX3','zoneY3','zoneX4','zoneY4'];
const ZONE_COORD_NAMES = ['x1','y1','x2','y2','x3','y3','x4','y4'];

// ---- Custom cluster definitions ----
const ld2450ConfigCluster = {
    ID: CLUSTER_CONFIG_ID,
    attributes: {
        targetCount:     {ID: 0x0000, type: ZCL_UINT8, report: true},
        targetCoords:    {ID: 0x0001, type: ZCL_CHAR_STR, report: true},
        maxDistance:      {ID: 0x0010, type: ZCL_UINT16, write: true},
        angleLeft:       {ID: 0x0011, type: ZCL_UINT8, write: true},
        angleRight:      {ID: 0x0012, type: ZCL_UINT8, write: true},
        trackingMode:    {ID: 0x0020, type: ZCL_UINT8, write: true},
        coordPublishing: {ID: 0x0021, type: ZCL_UINT8, write: true},
        restart:         {ID: 0x00F0, type: ZCL_UINT8, write: true},
    },
    commands: {},
    commandsResponse: {},
};

const ld2450ZoneCluster = {
    ID: CLUSTER_ZONE_ID,
    attributes: Object.fromEntries(
        ZONE_ATTR_NAMES.map((name, i) => [name, {ID: i, type: ZCL_INT16, write: true, report: true}])
    ),
    commands: {},
    commandsResponse: {},
};

function registerCustomClusters(device) {
    device.addCustomCluster('ld2450Config', ld2450ConfigCluster);
    device.addCustomCluster('ld2450Zone', ld2450ZoneCluster);
}

// ---- Expose helpers (inline, no require) ----

function binaryExpose(property, label, access, valueOn, valueOff, description, name) {
    return {type: 'binary', name: name || property, label, property, access,
        value_on: valueOn, value_off: valueOff, description};
}

function numericExpose(name, label, access, description, opts) {
    const e = {type: 'numeric', name, label, property: name, access, description};
    if (opts) Object.assign(e, opts);
    return e;
}

function enumExpose(name, label, access, values, description) {
    return {type: 'enum', name, label, property: name, access, values, description};
}

// ---- fromZigbee converters ----

const fzLocal = {
    occupancy: {
        cluster: 'msOccupancySensing',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            if (msg.data.occupancy === undefined) return {};
            const val = msg.data.occupancy === 1;
            const ep = msg.endpoint.ID;
            if (ep === 1) return {occupancy: val};
            const zone = ep - 1;
            if (zone >= 1 && zone <= 5) return {[`zone_${zone}_occupancy`]: val};
            return {};
        },
    },

    config: {
        cluster: 'ld2450Config',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            const result = {};
            const d = msg.data;

            if (d.targetCount !== undefined) result.target_count = d.targetCount;
            if (d.maxDistance !== undefined) result.max_distance = d.maxDistance / 1000;
            if (d.angleLeft !== undefined) result.angle_left = d.angleLeft;
            if (d.angleRight !== undefined) result.angle_right = d.angleRight;
            if (d.trackingMode !== undefined) {
                result.tracking_mode = d.trackingMode === 1 ? 'single' : 'multi';
            }
            if (d.coordPublishing !== undefined) {
                result.coord_publishing = d.coordPublishing === 1;
            }

            if (d.targetCoords !== undefined) {
                const str = d.targetCoords || '';
                const parts = str.split(';').filter(Boolean);
                for (let i = 0; i < 3; i++) {
                    if (i < parts.length) {
                        const [x, y] = parts[i].split(',').map(Number);
                        result[`target_${i + 1}_x`] = isNaN(x) ? 0 : x / 1000;
                        result[`target_${i + 1}_y`] = isNaN(y) ? 0 : y / 1000;
                    } else {
                        result[`target_${i + 1}_x`] = 0;
                        result[`target_${i + 1}_y`] = 0;
                    }
                }
            }

            return result;
        },
    },

    zone_vertices: {
        cluster: 'ld2450Zone',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            const zone = msg.endpoint.ID - 1;
            if (zone < 1 || zone > 5) return {};
            const result = {};
            for (let i = 0; i < 8; i++) {
                if (msg.data[ZONE_ATTR_NAMES[i]] !== undefined) {
                    result[`zone_${zone}_${ZONE_COORD_NAMES[i]}`] = msg.data[ZONE_ATTR_NAMES[i]] / 1000;
                }
            }
            return result;
        },
    },
};

// ---- toZigbee converters ----

const tzLocal = {
    config: {
        key: ['max_distance', 'angle_left', 'angle_right', 'tracking_mode', 'coord_publishing'],
        convertSet: async (entity, key, value, meta) => {
            registerCustomClusters(meta.device);
            const ep = meta.device.getEndpoint(1);
            const map = {
                max_distance:     {attr: 'maxDistance',      val: (v) => Math.round(v * 1000)},
                angle_left:       {attr: 'angleLeft',       val: (v) => v},
                angle_right:      {attr: 'angleRight',      val: (v) => v},
                tracking_mode:    {attr: 'trackingMode',    val: (v) => v === 'single' ? 1 : 0},
                coord_publishing: {attr: 'coordPublishing', val: (v) => v ? 1 : 0},
            };
            const m = map[key];
            await ep.write('ld2450Config', {[m.attr]: m.val(value)});
            return {state: {[key]: value}};
        },
        convertGet: async (entity, key, meta) => {
            registerCustomClusters(meta.device);
            const ep = meta.device.getEndpoint(1);
            const attrs = {
                max_distance: 'maxDistance', angle_left: 'angleLeft',
                angle_right: 'angleRight', tracking_mode: 'trackingMode',
                coord_publishing: 'coordPublishing',
            };
            await ep.read('ld2450Config', [attrs[key]]);
        },
    },

    restart: {
        key: ['restart'],
        convertSet: async (entity, key, value, meta) => {
            registerCustomClusters(meta.device);
            const ep = meta.device.getEndpoint(1);
            await ep.write('ld2450Config', {restart: 1});
        },
    },

    zone_vertices: {
        key: Array.from({length: 5}, (_, z) =>
            ZONE_COORD_NAMES.map(c => `zone_${z + 1}_${c}`)
        ).flat(),
        convertSet: async (entity, key, value, meta) => {
            registerCustomClusters(meta.device);
            const m = key.match(/^zone_(\d)_(x|y)(\d)$/);
            if (!m) return;
            const zone = parseInt(m[1]);
            const attrIdx = (parseInt(m[3]) - 1) * 2 + (m[2] === 'y' ? 1 : 0);
            const ep = meta.device.getEndpoint(zone + 1);
            await ep.write('ld2450Zone', {[ZONE_ATTR_NAMES[attrIdx]]: Math.round(value * 1000)});
            return {state: {[key]: value}};
        },
        convertGet: async (entity, key, meta) => {
            registerCustomClusters(meta.device);
            const m = key.match(/^zone_(\d)_(x|y)(\d)$/);
            if (!m) return;
            const ep = meta.device.getEndpoint(parseInt(m[1]) + 1);
            await ep.read('ld2450Zone', ZONE_ATTR_NAMES);
        },
    },
};

// ---- Expose definitions ----

const exposesDefinition = [
    binaryExpose('occupancy', 'Occupancy', ACCESS_STATE, true, false,
        'Overall presence detected in sensing area'),

    ...Array.from({length: 5}, (_, i) =>
        binaryExpose(`zone_${i + 1}_occupancy`, `Zone ${i + 1} occupancy`, ACCESS_STATE, true, false,
            `Presence detected in zone ${i + 1}`)
    ),

    numericExpose('target_count', 'Target count', ACCESS_STATE,
        'Number of detected targets', {value_min: 0, value_max: 3}),

    ...Array.from({length: 3}, (_, i) => [
        numericExpose(`target_${i + 1}_x`, `Target ${i + 1} X`, ACCESS_STATE,
            `Target ${i + 1} X coordinate`, {unit: 'm'}),
        numericExpose(`target_${i + 1}_y`, `Target ${i + 1} Y`, ACCESS_STATE,
            `Target ${i + 1} Y coordinate`, {unit: 'm'}),
    ]).flat(),

    numericExpose('max_distance', 'Max distance', ACCESS_ALL,
        'Maximum detection distance', {unit: 'm', value_min: 0, value_max: 6, value_step: 0.01}),

    numericExpose('angle_left', 'Angle left', ACCESS_ALL,
        'Left angle limit of detection zone', {unit: '°', value_min: 0, value_max: 90, value_step: 1}),

    numericExpose('angle_right', 'Angle right', ACCESS_ALL,
        'Right angle limit of detection zone', {unit: '°', value_min: 0, value_max: 90, value_step: 1}),

    enumExpose('tracking_mode', 'Tracking mode', ACCESS_ALL, ['multi', 'single'],
        'Multi-target or single-target tracking'),

    binaryExpose('coord_publishing', 'Coordinate publishing', ACCESS_ALL, true, false,
        'Enable publishing of target coordinates'),

    enumExpose('restart', 'Restart', ACCESS_SET, ['restart'],
        'Restart the device'),

    ...Array.from({length: 5}, (_, z) =>
        ZONE_COORD_NAMES.map(c => {
            const axis = c[0].toUpperCase();
            const point = c[1];
            return numericExpose(`zone_${z + 1}_${c}`, `Zone ${z + 1} - Point ${point} ${axis}`, ACCESS_ALL,
                `Zone ${z + 1} point ${point} ${axis} coordinate`, {unit: 'm', value_min: -6, value_max: 6, value_step: 0.01});
        })
    ).flat(),
];

// ---- Device definition ----

const definition = {
    zigbeeModel: ['LD2450-H2'],
    model: 'LD2450-ZB-H2',
    vendor: 'LD2450Z',
    description: 'HLK-LD2450 mmWave presence sensor (Zigbee, ESP32-H2)',
    fromZigbee: [fzLocal.occupancy, fzLocal.config, fzLocal.zone_vertices],
    toZigbee: [tzLocal.config, tzLocal.restart, tzLocal.zone_vertices],
    exposes: exposesDefinition,
    meta: {
        overrideHaDiscoveryPayload: (payload) => {
            if (payload.object_id && payload.object_id.endsWith('_occupancy')) {
                payload.device_class = 'occupancy';
            }
        },
    },
    onEvent: async (type, data, device) => {
        if (type === 'start' || type === 'deviceInterview') {
            registerCustomClusters(device);
        }
    },
    configure: async (device, coordinatorEndpoint) => {
        registerCustomClusters(device);

        const ep1 = device.getEndpoint(1);
        await ep1.bind('msOccupancySensing', coordinatorEndpoint);
        await ep1.configureReporting('msOccupancySensing', [
            {attribute: 'occupancy', minimumReportInterval: 0,
             maximumReportInterval: 300, reportableChange: 0},
        ]);
        await ep1.bind('ld2450Config', coordinatorEndpoint);
        await ep1.configureReporting('ld2450Config', [
            {attribute: 'targetCount', minimumReportInterval: 0,
             maximumReportInterval: 60, reportableChange: 1},
            {attribute: 'targetCoords', minimumReportInterval: 0,
             maximumReportInterval: 5},
        ]);
        await ep1.read('ld2450Config', [
            'targetCount', 'targetCoords', 'maxDistance', 'angleLeft', 'angleRight',
            'trackingMode', 'coordPublishing',
        ]);

        for (let z = 0; z < 5; z++) {
            const ep = device.getEndpoint(z + 2);
            await ep.bind('msOccupancySensing', coordinatorEndpoint);
            await ep.configureReporting('msOccupancySensing', [
                {attribute: 'occupancy', minimumReportInterval: 0,
                 maximumReportInterval: 300, reportableChange: 0},
            ]);
            await ep.bind('ld2450Zone', coordinatorEndpoint);
            await ep.read('ld2450Zone', ZONE_ATTR_NAMES);
        }
    },
};

module.exports = [definition];
