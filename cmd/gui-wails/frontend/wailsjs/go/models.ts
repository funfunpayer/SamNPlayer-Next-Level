export namespace device {

	export class Feature {
	    channel: string;
	    kind: string;
	    index: number;
	    actuator?: string;
	    driven: boolean;

	    static createFrom(source: any = {}) {
	        return new Feature(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.channel = source["channel"];
	        this.kind = source["kind"];
	        this.index = source["index"];
	        this.actuator = source["actuator"];
	        this.driven = source["driven"];
	    }
	}
	export class Capabilities {
	    device: string;
	    features: Feature[];
	    battery: boolean;

	    static createFrom(source: any = {}) {
	        return new Capabilities(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.device = source["device"];
	        this.features = (source["features"] || []).map((f: any) => new Feature(f));
	        this.battery = source["battery"];
	    }
	}

	export class DiagnosticsEntry {
	    timeOffsetMs: number;
	    phase: string;
	    channel?: string;
	    wantedValue?: number;
	    sentRaw?: number;
	    latencyMs: number;
	    error?: string;

	    static createFrom(source: any = {}) {
	        return new DiagnosticsEntry(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.timeOffsetMs = source["timeOffsetMs"];
	        this.phase = source["phase"];
	        this.channel = source["channel"];
	        this.wantedValue = source["wantedValue"];
	        this.sentRaw = source["sentRaw"];
	        this.latencyMs = source["latencyMs"];
	        this.error = source["error"];
	    }
	}
	export class DiagnosticsPhaseSummary {
	    phase: string;
	    commands: number;
	    errors: number;
	    meanLatencyMs: number;
	    maxLatencyMs: number;

	    static createFrom(source: any = {}) {
	        return new DiagnosticsPhaseSummary(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.phase = source["phase"];
	        this.commands = source["commands"];
	        this.errors = source["errors"];
	        this.meanLatencyMs = source["meanLatencyMs"];
	        this.maxLatencyMs = source["maxLatencyMs"];
	    }
	}
	export class DiagnosticsReport {
	    startedAt: string;
	    durationMs: number;
	    rawCapable: boolean;
	    phases: DiagnosticsPhaseSummary[];
	    log: DiagnosticsEntry[];
	    notes: string[];
	    interrupted: boolean;

	    static createFrom(source: any = {}) {
	        return new DiagnosticsReport(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.startedAt = source["startedAt"];
	        this.durationMs = source["durationMs"];
	        this.rawCapable = source["rawCapable"];
	        this.phases = this.convertValues(source["phases"], DiagnosticsPhaseSummary);
	        this.log = this.convertValues(source["log"], DiagnosticsEntry);
	        this.notes = source["notes"];
	        this.interrupted = source["interrupted"];
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}

}

export namespace funscript {

	export class Action {
	    at: number;
	    pos: number;

	    static createFrom(source: any = {}) {
	        return new Action(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.at = source["at"];
	        this.pos = source["pos"];
	    }
	}

	/** PairScore: Everyday/candidate vs FunGen reference (benchmark gut/nicht gut). */
	export class PairScore {
	    kind: string;
	    label: string;
	    passed: boolean;
	    detail: string;
	    video?: string;
	    reference?: string;
	    candidate?: string;
	    fidelity: any;
	    quality: any;
	    min_aligned_r: number;

	    static createFrom(source: any = {}) {
	        return new PairScore(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.kind = source["kind"];
	        this.label = source["label"];
	        this.passed = source["passed"];
	        this.detail = source["detail"];
	        this.video = source["video"];
	        this.reference = source["reference"];
	        this.candidate = source["candidate"];
	        this.fidelity = source["fidelity"];
	        this.quality = source["quality"];
	        this.min_aligned_r = source["min_aligned_r"];
	    }
	}

	export class SpeedSegment {
	    fromMs: number;
	    toMs: number;
	    intensity: number;

	    static createFrom(source: any = {}) {
	        return new SpeedSegment(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.fromMs = source["fromMs"];
	        this.toMs = source["toMs"];
	        this.intensity = source["intensity"];
	    }
	}

	export class Bookmark {
	    name: string;
	    time: number;
	    static createFrom(source: any = {}) { return new Bookmark(source); }
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.name = source["name"];
	        this.time = source["time"];
	    }
	}

	export class ChapterMark {
	    name: string;
	    startTime: number;
	    endTime: number;
	    static createFrom(source: any = {}) { return new ChapterMark(source); }
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.name = source["name"];
	        this.startTime = source["startTime"];
	        this.endTime = source["endTime"];
	    }
	}

	export class Project {
	    version: number;
	    videoPath: string;
	    scriptPath: string;
	    offsetMs: number;
	    seekMs: number;
	    static createFrom(source: any = {}) { return new Project(source); }
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.version = source["version"];
	        this.videoPath = source["videoPath"];
	        this.scriptPath = source["scriptPath"];
	        this.offsetMs = source["offsetMs"];
	        this.seekMs = source["seekMs"];
	    }
	}

	export class PipelineSuggestion {
	    Backend: string;
	    Profile: string;
	    Reason: string;
	    GoPath: boolean;

	    static createFrom(source: any = {}) {
	        return new PipelineSuggestion(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.Backend = source["Backend"];
	        this.Profile = source["Profile"];
	        this.Reason = source["Reason"];
	        this.GoPath = source["GoPath"];
	    }
	}

	export class OMarker {
	    startMs: number;
	    endMs: number;
	    kind: string;
	    intensity: number;

	    static createFrom(source: any = {}) {
	        return new OMarker(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.startMs = source["startMs"];
	        this.endMs = source["endMs"];
	        this.kind = source["kind"];
	        this.intensity = source["intensity"];
	    }
	}
	export class OZoneSuggestion {
	    startMs: number;
	    endMs: number;
	    reason: string;
	    ok: boolean;

	    static createFrom(source: any = {}) {
	        return new OZoneSuggestion(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.startMs = source["startMs"];
	        this.endMs = source["endMs"];
	        this.reason = source["reason"];
	        this.ok = source["ok"];
	    }
	}
	export class PolarityHint {
	    suggestInvert: boolean;
	    firstHalfMean: number;
	    reason: string;

	    static createFrom(source: any = {}) {
	        return new PolarityHint(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.suggestInvert = source["suggestInvert"];
	        this.firstHalfMean = source["firstHalfMean"];
	        this.reason = source["reason"];
	    }
	}

}

export namespace generator {

	export class ROI {
	    X: number;
	    Y: number;
	    W: number;
	    H: number;

	    static createFrom(source: any = {}) {
	        return new ROI(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.X = source["X"];
	        this.Y = source["Y"];
	        this.W = source["W"];
	        this.H = source["H"];
	    }
	}
	export class RoiTrainingRegion {
	    ROI: ROI;
	    ClassName: string;

	    static createFrom(source: any = {}) {
	        return new RoiTrainingRegion(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.ROI = this.convertValues(source["ROI"], ROI);
	        this.ClassName = source["ClassName"];
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class RoiTrainingDevice {
	    id: string;
	    label: string;
	    available: boolean;

	    static createFrom(source: any = {}) {
	        return new RoiTrainingDevice(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.id = source["id"];
	        this.label = source["label"];
	        this.available = source["available"];
	    }
	}
	export class BenchmarkCorrelation {
	    r: number;
	    lag_ms: number;
	    orientation: string;
	    n_samples: number;
	    shape_error?: number;
	    low_confidence: boolean;

	    static createFrom(source: any = {}) {
	        return new BenchmarkCorrelation(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.r = source["r"];
	        this.lag_ms = source["lag_ms"];
	        this.orientation = source["orientation"];
	        this.n_samples = source["n_samples"];
	        this.shape_error = source["shape_error"];
	        this.low_confidence = source["low_confidence"];
	    }
	}
	export class BenchmarkClipResult {
	    name: string;
	    ok: boolean;
	    error?: string;
	    quality_score?: number;
	    quality_passed?: boolean;
	    quality_warnings: string[];
	    correlation?: BenchmarkCorrelation;

	    static createFrom(source: any = {}) {
	        return new BenchmarkClipResult(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.name = source["name"];
	        this.ok = source["ok"];
	        this.error = source["error"];
	        this.quality_score = source["quality_score"];
	        this.quality_passed = source["quality_passed"];
	        this.quality_warnings = source["quality_warnings"];
	        this.correlation = this.convertValues(source["correlation"], BenchmarkCorrelation);
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class BenchmarkSummary {
	    total: number;
	    ok: number;
	    failed: number;
	    quality_passed: number;
	    mean_quality_score?: number;
	    mean_correlation?: number;
	    clips_with_reference: number;

	    static createFrom(source: any = {}) {
	        return new BenchmarkSummary(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.total = source["total"];
	        this.ok = source["ok"];
	        this.failed = source["failed"];
	        this.quality_passed = source["quality_passed"];
	        this.mean_quality_score = source["mean_quality_score"];
	        this.mean_correlation = source["mean_correlation"];
	        this.clips_with_reference = source["clips_with_reference"];
	    }
	}
	export class BenchmarkResult {
	    timestamp: string;
	    git_commit: string;
	    manifest: string;
	    clips: BenchmarkClipResult[];
	    summary: BenchmarkSummary;

	    static createFrom(source: any = {}) {
	        return new BenchmarkResult(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.timestamp = source["timestamp"];
	        this.git_commit = source["git_commit"];
	        this.manifest = source["manifest"];
	        this.clips = this.convertValues(source["clips"], BenchmarkClipResult);
	        this.summary = this.convertValues(source["summary"], BenchmarkSummary);
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class ProfileSuggestion {
	    found: boolean;
	    label: string;
	    kind: string;
	    confidence: number;
	
	    static createFrom(source: any = {}) {
	        return new ProfileSuggestion(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.found = source["found"];
	        this.label = source["label"];
	        this.kind = source["kind"];
	        this.confidence = source["confidence"];
	    }
	}
	export class MapWindowDTO {
	    startMs: number;
	    endMs: number;
	    tempoHz: number;
	    score: number[];
	    chosenCell: number;
	    boxCx: number;
	    boxCy: number;
	    signRule: string;
	    trackerR: number;

	    static createFrom(source: any = {}) {
	        return new MapWindowDTO(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.startMs = source["startMs"];
	        this.endMs = source["endMs"];
	        this.tempoHz = source["tempoHz"];
	        this.score = source["score"];
	        this.chosenCell = source["chosenCell"];
	        this.boxCx = source["boxCx"];
	        this.boxCy = source["boxCy"];
	        this.signRule = source["signRule"];
	        this.trackerR = source["trackerR"];
	    }
	}
	export class SceneMapDTO {
	    version: number;
	    cols: number;
	    rows: number;
	    width: number;
	    height: number;
	    windows: MapWindowDTO[];

	    static createFrom(source: any = {}) {
	        return new SceneMapDTO(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.version = source["version"];
	        this.cols = source["cols"];
	        this.rows = source["rows"];
	        this.width = source["width"];
	        this.height = source["height"];
	        this.windows = this.convertValues(source["windows"], MapWindowDTO);
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class SceneMark {
	    kind: string;
	    id: string;
	    rect: ROI;
	    fromMs: number;
	    toMs: number;
	    class: string;
	    author?: string;
	    follow?: boolean;

	    static createFrom(source: any = {}) {
	        return new SceneMark(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.kind = source["kind"];
	        this.id = source["id"];
	        this.rect = this.convertValues(source["rect"], ROI);
	        this.fromMs = source["fromMs"];
	        this.toMs = source["toMs"];
	        this.class = source["class"];
	        this.author = source["author"];
	        this.follow = source["follow"];
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class SceneMapLoad {
	    path: string;
	    map: SceneMapDTO;
	    marks: SceneMark[];
	    found: boolean;

	    static createFrom(source: any = {}) {
	        return new SceneMapLoad(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.path = source["path"];
	        this.map = this.convertValues(source["map"], SceneMapDTO);
	        this.marks = this.convertValues(source["marks"], SceneMark);
	        this.found = source["found"];
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class LearningExportResult {
	    outDir: string;
	    windows: number;
	    negatives: number;
	    autoCandidates: number;
	    userRegionMarks: number;
	    tracePath: string;
	    negativesPath: string;
	    autoPath: string;
	    userRegionsPath: string;

	    static createFrom(source: any = {}) {
	        return new LearningExportResult(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.outDir = source["outDir"];
	        this.windows = source["windows"];
	        this.negatives = source["negatives"];
	        this.autoCandidates = source["autoCandidates"];
	        this.userRegionMarks = source["userRegionMarks"];
	        this.tracePath = source["tracePath"];
	        this.negativesPath = source["negativesPath"];
	        this.autoPath = source["autoPath"];
	        this.userRegionsPath = source["userRegionsPath"];
	    }
	}
	export class ExcludePriorSuggestion {
	    mark: SceneMark;
	    share: number;
	    clipCount: number;
	    cellCol: number;
	    cellRow: number;
	    confidence: number;

	    static createFrom(source: any = {}) {
	        return new ExcludePriorSuggestion(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.mark = this.convertValues(source["mark"], SceneMark);
	        this.share = source["share"];
	        this.clipCount = source["clipCount"];
	        this.cellCol = source["cellCol"];
	        this.cellRow = source["cellRow"];
	        this.confidence = source["confidence"];
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class ExcludePriorResult {
	    suggestions: ExcludePriorSuggestion[];
	    clipsSeen: number;
	    decisions: number;
	    note: string;
	    learningDir: string;

	    static createFrom(source: any = {}) {
	        return new ExcludePriorResult(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.suggestions = this.convertValues(source["suggestions"], ExcludePriorSuggestion);
	        this.clipsSeen = source["clipsSeen"];
	        this.decisions = source["decisions"];
	        this.note = source["note"];
	        this.learningDir = source["learningDir"];
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class ScriptQualityResult {
	    score: number;
	    passed: boolean;
	    warnings: string[];
	    estimatedFromScriptOnly: boolean;

	    static createFrom(source: any = {}) {
	        return new ScriptQualityResult(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.score = source["score"];
	        this.passed = source["passed"];
	        this.warnings = source["warnings"];
	        this.estimatedFromScriptOnly = source["estimatedFromScriptOnly"];
	    }
	}

}

export namespace legal {
	
	export class DonateLink {
	    name: string;
	    url: string;
	
	    static createFrom(source: any = {}) {
	        return new DonateLink(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.name = source["name"];
	        this.url = source["url"];
	    }
	}

}

export namespace main {
	
	export class AboutInfo {
	    version: string;
	    edition: string;
	    eulaVersion: string;
	    partnerName: string;
	    partnerUrl: string;
	
	    static createFrom(source: any = {}) {
	        return new AboutInfo(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.version = source["version"];
	        this.edition = source["edition"];
	        this.eulaVersion = source["eulaVersion"];
	        this.partnerName = source["partnerName"];
	        this.partnerUrl = source["partnerUrl"];
	    }
	}
	
	export class EULAInfo {
	    version: string;
	    text: string;
	    accepted: boolean;
	
	    static createFrom(source: any = {}) {
	        return new EULAInfo(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.version = source["version"];
	        this.text = source["text"];
	        this.accepted = source["accepted"];
	    }
	}
	
	export class BakeOffsetResult {
	    shiftedMs: number;
	    dropped: number;
	
	    static createFrom(source: any = {}) {
	        return new BakeOffsetResult(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.shiftedMs = source["shiftedMs"];
	        this.dropped = source["dropped"];
	    }
	}

	export class BenchmarkClipExportRequest {
	    source: string;
	    output: string;
	    start: string;
	    end: string;
	    startSec?: number;
	    endSec?: number;
	    maxWidth?: number;
	    presetRes?: string;
	    noAudio?: boolean;

	    static createFrom(source: any = {}) {
	        return new BenchmarkClipExportRequest(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.source = source["source"];
	        this.output = source["output"];
	        this.start = source["start"];
	        this.end = source["end"];
	        this.startSec = source["startSec"];
	        this.endSec = source["endSec"];
	        this.maxWidth = source["maxWidth"];
	        this.presetRes = source["presetRes"];
	        this.noAudio = source["noAudio"];
	    }
	}

	export class BenchmarkClipExportResult {
	    output: string;
	    startSec: number;
	    endSec: number;
	    durationSec: number;
	    maxWidth: number;

	    static createFrom(source: any = {}) {
	        return new BenchmarkClipExportResult(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.output = source["output"];
	        this.startSec = source["startSec"];
	        this.endSec = source["endSec"];
	        this.durationSec = source["durationSec"];
	        this.maxWidth = source["maxWidth"];
	    }
	}

	export class BenchmarkPairSuggestion {
	    video: string;
	    reference?: string;
	    candidate?: string;
	    references?: string[];
	    candidates?: string[];
	    note?: string;

	    static createFrom(source: any = {}) {
	        return new BenchmarkPairSuggestion(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.video = source["video"];
	        this.reference = source["reference"];
	        this.candidate = source["candidate"];
	        this.references = source["references"];
	        this.candidates = source["candidates"];
	        this.note = source["note"];
	    }
	}

	export class SceneProposalLoad {
	    found: boolean;
	    path: string;
	    width: number;
	    height: number;
	    count: number;
	    proposal: any;
	    regionClass: string;

	    static createFrom(source: any = {}) {
	        return new SceneProposalLoad(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.found = source["found"];
	        this.path = source["path"];
	        this.width = source["width"];
	        this.height = source["height"];
	        this.count = source["count"];
	        this.proposal = source["proposal"];
	        this.regionClass = source["regionClass"];
	    }
	}

	export class CacheInfo {
	    path: string;
	    files: number;
	    bytes: number;
	    humanSize: string;
	    clearOnExit: boolean;
	
	    static createFrom(source: any = {}) {
	        return new CacheInfo(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.path = source["path"];
	        this.files = source["files"];
	        this.bytes = source["bytes"];
	        this.humanSize = source["humanSize"];
	        this.clearOnExit = source["clearOnExit"];
	    }
	}
	export class CurvePoint {
	    atMs: number;
	    pos: number;
	
	    static createFrom(source: any = {}) {
	        return new CurvePoint(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.atMs = source["atMs"];
	        this.pos = source["pos"];
	    }
	}
	export class VibrationCurvePoint {
	    atMs: number;
	    vibration: number;

	    static createFrom(source: any = {}) {
	        return new VibrationCurvePoint(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.atMs = source["atMs"];
	        this.vibration = source["vibration"];
	    }
	}
	export class AudioSyncSuggestion {
	    offsetMs: number;
	    currentMs: number;
	    r: number;
	    margin: number;
	    confidence: string;
	    note: string;

	    static createFrom(source: any = {}) {
	        return new AudioSyncSuggestion(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.offsetMs = source["offsetMs"];
	        this.currentMs = source["currentMs"];
	        this.r = source["r"];
	        this.margin = source["margin"];
	        this.confidence = source["confidence"];
	        this.note = source["note"];
	    }
	}
	export class CreateVideoInfo {
	    path: string;
	    name: string;
	    width: number;
	    height: number;
	    fps: number;
	    vfr: boolean;
	    durationSec: number;
	    codec: string;
	    posterUrl: string;

	    static createFrom(source: any = {}) {
	        return new CreateVideoInfo(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.path = source["path"];
	        this.name = source["name"];
	        this.width = source["width"];
	        this.height = source["height"];
	        this.fps = source["fps"];
	        this.vfr = source["vfr"];
	        this.durationSec = source["durationSec"];
	        this.codec = source["codec"];
	        this.posterUrl = source["posterUrl"];
	    }
	}
	export class VideoThumb {
	    atMs: number;
	    dataUrl: string;

	    static createFrom(source: any = {}) {
	        return new VideoThumb(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.atMs = source["atMs"];
	        this.dataUrl = source["dataUrl"];
	    }
	}
	export class DeviceStatus {
	    connected: boolean;
	    mock: boolean;
	    name: string;
	    address: string;
	    rssi: number;
	    sessionActive: boolean;
	    capabilities?: device.Capabilities;

	    static createFrom(source: any = {}) {
	        return new DeviceStatus(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.connected = source["connected"];
	        this.mock = source["mock"];
	        this.name = source["name"];
	        this.address = source["address"];
	        this.rssi = source["rssi"];
	        this.sessionActive = source["sessionActive"];
	        this.capabilities = source["capabilities"] ? new device.Capabilities(source["capabilities"]) : undefined;
	    }
	}
	export class RuntimeDepInfo {
	    id: string;
	    label: string;
	    required: boolean;
	    found: boolean;
	    path: string;
	    hint: string;

	    static createFrom(source: any = {}) {
	        return new RuntimeDepInfo(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.id = source["id"];
	        this.label = source["label"];
	        this.required = source["required"];
	        this.found = source["found"];
	        this.path = source["path"];
	        this.hint = source["hint"];
	    }
	}
	export class RuntimeHealth {
	    dirsCreated: string[];
	    dirsFailed: string[];
	    deps: RuntimeDepInfo[];
	    ok: boolean;

	    static createFrom(source: any = {}) {
	        return new RuntimeHealth(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.dirsCreated = source["dirsCreated"];
	        this.dirsFailed = source["dirsFailed"];
	        this.deps = this.convertValues(source["deps"], RuntimeDepInfo);
	        this.ok = source["ok"];
	    }

	    convertValues(a: any, classs: any, asMap: boolean = false): any {
	        if (!a) {
	            return a;
	        }
	        if (a.slice && a.map) {
	            return (a as any[]).map(elem => this.convertValues(elem, classs));
	        } else if ("object" === typeof a) {
	            if (asMap) {
	                for (const key of Object.keys(a)) {
	                    a[key] = new classs(a[key]);
	                }
	                return a;
	            }
	            return new classs(a);
	        }
	        return a;
	    }
	}
	export class DiagnosticsHistoryEntry {
	    timestamp: string;
	    mock: boolean;
	    deviceName: string;
	    report: device.DiagnosticsReport;

	    static createFrom(source: any = {}) {
	        return new DiagnosticsHistoryEntry(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.timestamp = source["timestamp"];
	        this.mock = source["mock"];
	        this.deviceName = source["deviceName"];
	        this.report = this.convertValues(source["report"], device.DiagnosticsReport);
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class RoiTrainingBox {
	    classId: number;
	    className: string;
	    xc: number;
	    yc: number;
	    w: number;
	    h: number;

	    static createFrom(source: any = {}) {
	        return new RoiTrainingBox(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.classId = source["classId"];
	        this.className = source["className"];
	        this.xc = source["xc"];
	        this.yc = source["yc"];
	        this.w = source["w"];
	        this.h = source["h"];
	    }
	}
	export class RoiTrainingSample {
	    split: string;
	    name: string;
	    imagePath: string;
	    boxes: RoiTrainingBox[];

	    static createFrom(source: any = {}) {
	        return new RoiTrainingSample(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.split = source["split"];
	        this.name = source["name"];
	        this.imagePath = source["imagePath"];
	        this.boxes = this.convertValues(source["boxes"], RoiTrainingBox);
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class RoiClassCount {
	    className: string;
	    classId: number;
	    trainCount: number;
	    valCount: number;

	    static createFrom(source: any = {}) {
	        return new RoiClassCount(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.className = source["className"];
	        this.classId = source["classId"];
	        this.trainCount = source["trainCount"];
	        this.valCount = source["valCount"];
	    }
	}
	export class RoiDatasetSummary {
	    datasetDir: string;
	    classes: RoiClassCount[];

	    static createFrom(source: any = {}) {
	        return new RoiDatasetSummary(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.datasetDir = source["datasetDir"];
	        this.classes = this.convertValues(source["classes"], RoiClassCount);
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class FramePreview {
	    width: number;
	    height: number;
	    pngBase64: string;

	    static createFrom(source: any = {}) {
	        return new FramePreview(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.width = source["width"];
	        this.height = source["height"];
	        this.pngBase64 = source["pngBase64"];
	    }
	}
	export class PostprocessPreviewRequest {
	    smoothWindow: number;
	    minPeakDistanceMs: number;
	    peakProminence: number;
	    rdpTolerance: number;
	    adaptiveKeyframeError: number;
	    maxSpeed: number;

	    static createFrom(source: any = {}) {
	        return new PostprocessPreviewRequest(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.smoothWindow = source["smoothWindow"];
	        this.minPeakDistanceMs = source["minPeakDistanceMs"];
	        this.peakProminence = source["peakProminence"];
	        this.rdpTolerance = source["rdpTolerance"];
	        this.adaptiveKeyframeError = source["adaptiveKeyframeError"];
	        this.maxSpeed = source["maxSpeed"];
	    }
	}
	export class ContactVibPreviewRequest {
	    span: number;
	    curve: string;

	    static createFrom(source: any = {}) {
	        return new ContactVibPreviewRequest(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.span = source["span"];
	        this.curve = source["curve"];
	    }
	}
	export class ContactVibPreviewResult {
	    sample: any[];
	    hint: string;
	    peakVib: number;
	    activePct: number;

	    static createFrom(source: any = {}) {
	        return new ContactVibPreviewResult(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.sample = source["sample"];
	        this.hint = source["hint"];
	        this.peakVib = source["peakVib"];
	        this.activePct = source["activePct"];
	    }
	}
	export class PostprocessPreviewResult {
	    keyframeCount: number;
	    peakCount: number;
	    valleyCount: number;
	    meanHz: number;
	    hint: string;

	    static createFrom(source: any = {}) {
	        return new PostprocessPreviewResult(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.keyframeCount = source["keyframeCount"];
	        this.peakCount = source["peakCount"];
	        this.valleyCount = source["valleyCount"];
	        this.meanHz = source["meanHz"];
	        this.hint = source["hint"];
	    }
	}
	export class GenerateOptions {
	    videoPath: string;
	    x: number;
	    y: number;
	    w: number;
	    h: number;
	    x2: number;
	    y2: number;
	    w2: number;
	    h2: number;
	    invert: boolean;
	    smoothWindow: number;
	    minPeakDistanceMs: number;
	    peakProminence: number;
	    disableCameraCompensation: boolean;
	    disableSceneCutDetection: boolean;
	    rdpTolerance: number;
	    perSceneRoi: boolean;
	    adaptiveKeyframeError: number;
	    maxSpeed: number;
	    axis: string;
	    autoRetry: boolean;
	    backend: string;
	    useOpenCl: boolean;
	    dynamicRangeMs: number;
	    profile: string;
	    overwrite: boolean;
	    aiQualityOpinion: boolean;
	    contactVibration: boolean;
	    contactVibrationSpan: number;
	    contactVibrationCurve: string;
	    autoOZoneMarker: boolean;
	    audioCheck: boolean;
	    nativePipeline: boolean;
	    startTimeSec: number;

	    static createFrom(source: any = {}) {
	        return new GenerateOptions(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.videoPath = source["videoPath"];
	        this.x = source["x"];
	        this.y = source["y"];
	        this.w = source["w"];
	        this.h = source["h"];
	        this.x2 = source["x2"];
	        this.y2 = source["y2"];
	        this.w2 = source["w2"];
	        this.h2 = source["h2"];
	        this.invert = source["invert"];
	        this.smoothWindow = source["smoothWindow"];
	        this.minPeakDistanceMs = source["minPeakDistanceMs"];
	        this.peakProminence = source["peakProminence"];
	        this.disableCameraCompensation = source["disableCameraCompensation"];
	        this.disableSceneCutDetection = source["disableSceneCutDetection"];
	        this.rdpTolerance = source["rdpTolerance"];
	        this.perSceneRoi = source["perSceneRoi"];
	        this.adaptiveKeyframeError = source["adaptiveKeyframeError"];
	        this.maxSpeed = source["maxSpeed"];
	        this.axis = source["axis"];
	        this.autoRetry = source["autoRetry"];
	        this.backend = source["backend"];
	        this.useOpenCl = source["useOpenCl"];
	        this.dynamicRangeMs = source["dynamicRangeMs"];
	        this.profile = source["profile"];
	        this.overwrite = source["overwrite"];
	        this.aiQualityOpinion = source["aiQualityOpinion"];
	        this.contactVibration = source["contactVibration"];
	        this.contactVibrationSpan = source["contactVibrationSpan"];
	        this.contactVibrationCurve = source["contactVibrationCurve"];
	        this.autoOZoneMarker = source["autoOZoneMarker"];
	        this.audioCheck = source["audioCheck"];
	        this.nativePipeline = source["nativePipeline"];
	        this.startTimeSec = source["startTimeSec"];
	    }
	}
	export class GeneratedReview {
	    path: string;
	    polarity: funscript.PolarityHint;
	    ozone: funscript.OZoneSuggestion;

	    static createFrom(source: any = {}) {
	        return new GeneratedReview(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.path = source["path"];
	        this.polarity = this.convertValues(source["polarity"], funscript.PolarityHint);
	        this.ozone = this.convertValues(source["ozone"], funscript.OZoneSuggestion);
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class HeatmapPoint {
	    atMs: number;
	    intensity: number;
	
	    static createFrom(source: any = {}) {
	        return new HeatmapPoint(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.atMs = source["atMs"];
	        this.intensity = source["intensity"];
	    }
	}
	export class Marker {
	    startMs: number;
	    endMs: number;
	
	    static createFrom(source: any = {}) {
	        return new Marker(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.startMs = source["startMs"];
	        this.endMs = source["endMs"];
	    }
	}
	export class ContactPreviewOptions {
	    maxPoints: number;
	    contactVibrationSpan: number;
	    contactVibrationCurve: string;
	    contactIntensityScale: number;
	    muteContact: boolean;

	    static createFrom(source: any = {}) {
	        return new ContactPreviewOptions(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.maxPoints = source["maxPoints"];
	        this.contactVibrationSpan = source["contactVibrationSpan"];
	        this.contactVibrationCurve = source["contactVibrationCurve"];
	        this.contactIntensityScale = source["contactIntensityScale"];
	        this.muteContact = source["muteContact"];
	    }
	}
	export class PlaybackOptions {
	    mock: boolean;
	    syncMode: string;
	    tickMs: number;
	    maxSpeed: number;
	    smoothing: number;
	    softStartMs: number;
	    useVideoSync: boolean;
	    extendedOEnabled: boolean;
	    extendedOMin: number;
	    extendedOHoldS: number;
	    extendedORestoreMs: number;
	    disableContactVibration: boolean;
	    contactIntensityScale: number;
	    contactExtraSmooth: number;
	    contactVibrationSpan: number;
	    contactVibrationCurve: string;
	    speed: number;
	
	    static createFrom(source: any = {}) {
	        return new PlaybackOptions(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.mock = source["mock"];
	        this.syncMode = source["syncMode"];
	        this.tickMs = source["tickMs"];
	        this.maxSpeed = source["maxSpeed"];
	        this.smoothing = source["smoothing"];
	        this.softStartMs = source["softStartMs"];
	        this.useVideoSync = source["useVideoSync"];
	        this.extendedOEnabled = source["extendedOEnabled"];
	        this.extendedOMin = source["extendedOMin"];
	        this.extendedOHoldS = source["extendedOHoldS"];
	        this.extendedORestoreMs = source["extendedORestoreMs"];
	        this.disableContactVibration = source["disableContactVibration"];
	        this.contactIntensityScale = source["contactIntensityScale"];
	        this.contactExtraSmooth = source["contactExtraSmooth"];
	        this.contactVibrationSpan = source["contactVibrationSpan"];
	        this.contactVibrationCurve = source["contactVibrationCurve"];
	        this.speed = source["speed"];
	    }
	}
	export class ReportFeedback {
	    outputPath: string;
	    verdict: string;
	    comment: string;
	
	    static createFrom(source: any = {}) {
	        return new ReportFeedback(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.outputPath = source["outputPath"];
	        this.verdict = source["verdict"];
	        this.comment = source["comment"];
	    }
	}
	export class ScriptSegment {
	    state: string;
	    startMs: number;
	    endMs: number;
	    meanSpeed: number;
	
	    static createFrom(source: any = {}) {
	        return new ScriptSegment(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.state = source["state"];
	        this.startMs = source["startMs"];
	        this.endMs = source["endMs"];
	        this.meanSpeed = source["meanSpeed"];
	    }
	}
	export class ScriptAnalysis {
	    segments: ScriptSegment[];
	    shareByState: Record<string, number>;
	    totalMs: number;
	    summary: string;
	
	    static createFrom(source: any = {}) {
	        return new ScriptAnalysis(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.segments = this.convertValues(source["segments"], ScriptSegment);
	        this.shareByState = source["shareByState"];
	        this.totalMs = source["totalMs"];
	        this.summary = source["summary"];
	    }
	
		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class ScriptInfo {
	    path: string;
	    actionCount: number;
	    durationMs: number;
	    videoPath: string;
	    hasVideo: boolean;
	    profile: string;
	    contactVibration: boolean;
	    contactVibrationSpan: number;
	    contactVibrationCurve: string;
	
	    static createFrom(source: any = {}) {
	        return new ScriptInfo(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.path = source["path"];
	        this.actionCount = source["actionCount"];
	        this.durationMs = source["durationMs"];
	        this.videoPath = source["videoPath"];
	        this.hasVideo = source["hasVideo"];
	        this.profile = source["profile"];
	        this.contactVibration = source["contactVibration"];
	        this.contactVibrationSpan = source["contactVibrationSpan"];
	        this.contactVibrationCurve = source["contactVibrationCurve"];
	    }
	}
	
	export class Settings {
	    updateCheckOnStartup: boolean;
	    logLevel: string;
	    playbackMock: boolean;
	    playbackSync: string;
	    playbackTickMs: number;
	    playbackMaxSpeed: number;
	    playbackSmoothing: number;
	    playbackSoftStartMs: number;
	    playbackEffectPresets: string;
	    playbackEOEnabled: boolean;
	    playbackEOMin: number;
	    playbackEOHoldS: number;
	    playbackEORestoreMs: number;
	    playbackVideoPlayAutostart: boolean;
	    playbackTrajectoryOverlay: boolean;
	    playbackCapIntensity: number;
	    playbackSpeedHL: boolean;
	    playbackSpeedHLThresh: number;
	    playbackScaleFactor: number;
	    playbackScaleSoftEdges: boolean;
	    playbackFpsSnap: number;
	    playbackBpmGrid: boolean;
	    playbackOMarkerIntensity: number;
	    playbackFeelHeatbands: boolean;
	    playbackFeelHeatbandsOpacity: number;
	    playbackContactIntensity: number;
	    trainingMock: boolean;
	    trainingTechnique: string;
	    trainingChannel: string;
	    trainingCycles: number;
	    trainingRampUpMs: number;
	    trainingHoldMs: number;
	    trainingRestMs: number;
	    trainingPeakIntensity: number;
	    trainingPlateauFraction: number;
	    trainingProgressionPerCycle: number;
	    logPath: string;
	    reportPath: string;
	    defaultReportPath: string;
	    clearCacheOnExit: boolean;
	    deviceTransport: string;
	    intifaceUrl: string;
	    tcodeAddress: string;
	    deviceConnectTest: boolean;
	    aiRoiModelPath: string;
	    aiBaseUrl: string;
	    benchmarkManifestPath: string;
	    benchmarkHistoryPath: string;
	    defaultBenchmarkHistoryPath: string;
	    diagnosticsHistoryPath: string;
	    defaultDiagnosticsHistoryPath: string;
	    roiDatasetDir: string;
	    defaultRoiDatasetDir: string;
	    collectLearningData: boolean;
	    applyAISetupAutomatically: boolean;
	    genContactVibrationSpan: number;
	    genContactVibrationCurve: string;
	    genSmoothWindow: number;
	    genMinPeakDistanceMs: number;
	    genPeakProminence: number;
	    genRDPTolerance: number;
	    genMaxSpeed: number;
	    genAdaptiveKeyframe: boolean;
	    roiSampleEvery: number;
	    roiBoxScale: number;

	    static createFrom(source: any = {}) {
	        return new Settings(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.updateCheckOnStartup = source["updateCheckOnStartup"];
	        this.logLevel = source["logLevel"];
	        this.playbackMock = source["playbackMock"];
	        this.playbackSync = source["playbackSync"];
	        this.playbackTickMs = source["playbackTickMs"];
	        this.playbackMaxSpeed = source["playbackMaxSpeed"];
	        this.playbackSmoothing = source["playbackSmoothing"];
	        this.playbackSoftStartMs = source["playbackSoftStartMs"];
	        this.playbackEffectPresets = source["playbackEffectPresets"];
	        this.playbackEOEnabled = source["playbackEOEnabled"];
	        this.playbackEOMin = source["playbackEOMin"];
	        this.playbackEOHoldS = source["playbackEOHoldS"];
	        this.playbackEORestoreMs = source["playbackEORestoreMs"];
	        this.playbackVideoPlayAutostart = source["playbackVideoPlayAutostart"];
	        this.playbackTrajectoryOverlay = source["playbackTrajectoryOverlay"];
	        this.playbackCapIntensity = source["playbackCapIntensity"];
	        this.playbackSpeedHL = source["playbackSpeedHL"];
	        this.playbackSpeedHLThresh = source["playbackSpeedHLThresh"];
	        this.playbackScaleFactor = source["playbackScaleFactor"];
	        this.playbackScaleSoftEdges = source["playbackScaleSoftEdges"];
	        this.playbackFpsSnap = source["playbackFpsSnap"];
	        this.playbackBpmGrid = source["playbackBpmGrid"];
	        this.playbackOMarkerIntensity = source["playbackOMarkerIntensity"];
	        this.playbackFeelHeatbands = source["playbackFeelHeatbands"];
	        this.playbackFeelHeatbandsOpacity = source["playbackFeelHeatbandsOpacity"];
	        this.playbackContactIntensity = source["playbackContactIntensity"];
	        this.trainingMock = source["trainingMock"];
	        this.trainingTechnique = source["trainingTechnique"];
	        this.trainingChannel = source["trainingChannel"];
	        this.trainingCycles = source["trainingCycles"];
	        this.trainingRampUpMs = source["trainingRampUpMs"];
	        this.trainingHoldMs = source["trainingHoldMs"];
	        this.trainingRestMs = source["trainingRestMs"];
	        this.trainingPeakIntensity = source["trainingPeakIntensity"];
	        this.trainingPlateauFraction = source["trainingPlateauFraction"];
	        this.trainingProgressionPerCycle = source["trainingProgressionPerCycle"];
	        this.logPath = source["logPath"];
	        this.reportPath = source["reportPath"];
	        this.defaultReportPath = source["defaultReportPath"];
	        this.clearCacheOnExit = source["clearCacheOnExit"];
	        this.deviceTransport = source["deviceTransport"];
	        this.intifaceUrl = source["intifaceUrl"];
	        this.tcodeAddress = source["tcodeAddress"];
	        this.deviceConnectTest = source["deviceConnectTest"];
	        this.aiRoiModelPath = source["aiRoiModelPath"];
	        this.aiBaseUrl = source["aiBaseUrl"];
	        this.benchmarkManifestPath = source["benchmarkManifestPath"];
	        this.benchmarkHistoryPath = source["benchmarkHistoryPath"];
	        this.defaultBenchmarkHistoryPath = source["defaultBenchmarkHistoryPath"];
	        this.diagnosticsHistoryPath = source["diagnosticsHistoryPath"];
	        this.defaultDiagnosticsHistoryPath = source["defaultDiagnosticsHistoryPath"];
	        this.roiDatasetDir = source["roiDatasetDir"];
	        this.defaultRoiDatasetDir = source["defaultRoiDatasetDir"];
	        this.collectLearningData = source["collectLearningData"];
	        this.applyAISetupAutomatically = source["applyAISetupAutomatically"];
	        this.genContactVibrationSpan = source["genContactVibrationSpan"];
	        this.genContactVibrationCurve = source["genContactVibrationCurve"];
	        this.genSmoothWindow = source["genSmoothWindow"];
	        this.genMinPeakDistanceMs = source["genMinPeakDistanceMs"];
	        this.genPeakProminence = source["genPeakProminence"];
	        this.genRDPTolerance = source["genRDPTolerance"];
	        this.genMaxSpeed = source["genMaxSpeed"];
	        this.genAdaptiveKeyframe = source["genAdaptiveKeyframe"];
	        this.roiSampleEvery = source["roiSampleEvery"];
	        this.roiBoxScale = source["roiBoxScale"];
	    }
	}
	export class TrainingRequest {
	    mock: boolean;
	    intensityFactor: number;
	    technique: string;
	    channel: string;
	    cycles: number;
	    rampUpMs: number;
	    holdMs: number;
	    restMs: number;
	    peakIntensity: number;
	    plateauFraction: number;
	    progressionPerCycle: number;
	
	    static createFrom(source: any = {}) {
	        return new TrainingRequest(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.mock = source["mock"];
	        this.intensityFactor = source["intensityFactor"];
	        this.technique = source["technique"];
	        this.channel = source["channel"];
	        this.cycles = source["cycles"];
	        this.rampUpMs = source["rampUpMs"];
	        this.holdMs = source["holdMs"];
	        this.restMs = source["restMs"];
	        this.peakIntensity = source["peakIntensity"];
	        this.plateauFraction = source["plateauFraction"];
	        this.progressionPerCycle = source["progressionPerCycle"];
	    }
	}
	export class TrainingSessionSummary {
	    fileName: string;
	    startedAt: string;
	    technique: string;
	    channel: string;
	    cyclesCompleted: number;
	    cyclesStoppedEarly: number;
	    meanPeakIntensity: number;
	    meanReachedPeakAfterMs: number;
	    meanArousalReported: number;
	    arousalReportsCount: number;

	    static createFrom(source: any = {}) {
	        return new TrainingSessionSummary(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.fileName = source["fileName"];
	        this.startedAt = source["startedAt"];
	        this.technique = source["technique"];
	        this.channel = source["channel"];
	        this.cyclesCompleted = source["cyclesCompleted"];
	        this.cyclesStoppedEarly = source["cyclesStoppedEarly"];
	        this.meanPeakIntensity = source["meanPeakIntensity"];
	        this.meanReachedPeakAfterMs = source["meanReachedPeakAfterMs"];
	        this.meanArousalReported = source["meanArousalReported"];
	        this.arousalReportsCount = source["arousalReportsCount"];
	    }
	}
	export class UpdateCheckResult {
	    available: boolean;
	    kind?: string;
	    release?: update.Release;
	    patch?: update.Patch;
	    patchId?: string;
	    assetName?: string;
	    error?: string;
	
	    static createFrom(source: any = {}) {
	        return new UpdateCheckResult(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.available = source["available"];
	        this.kind = source["kind"];
	        this.release = this.convertValues(source["release"], update.Release);
	        this.patch = this.convertValues(source["patch"], update.Patch);
	        this.patchId = source["patchId"];
	        this.assetName = source["assetName"];
	        this.error = source["error"];
	    }
	
		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}

}

export namespace motionx {

	export class Chapter {
	    kind: string;
	    startMs: number;
	    endMs: number;

	    static createFrom(source: any = {}) {
	        return new Chapter(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.kind = source["kind"];
	        this.startMs = source["startMs"];
	        this.endMs = source["endMs"];
	    }
	}

}

export namespace update {
	
	export class Asset {
	    name: string;
	    browser_download_url: string;
	    size: number;
	
	    static createFrom(source: any = {}) {
	        return new Asset(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.name = source["name"];
	        this.browser_download_url = source["browser_download_url"];
	        this.size = source["size"];
	    }
	}
	export class Patch {
	    id: string;
	    tag_name: string;
	    base_version: string;
	    patch_number: number;
	    html_url: string;
	    body?: string;
	    assets: Asset[];

	    static createFrom(source: any = {}) {
	        return new Patch(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.id = source["id"];
	        this.tag_name = source["tag_name"];
	        this.base_version = source["base_version"];
	        this.patch_number = source["patch_number"];
	        this.html_url = source["html_url"];
	        this.body = source["body"];
	        this.assets = this.convertValues(source["assets"], Asset);
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class Release {
	    tag_name: string;
	    html_url: string;
	    body?: string;
	    assets: Asset[];
	
	    static createFrom(source: any = {}) {
	        return new Release(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.tag_name = source["tag_name"];
	        this.html_url = source["html_url"];
	        this.body = source["body"];
	        this.assets = this.convertValues(source["assets"], Asset);
	    }
	
		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}

}

export namespace profilemodel {

	export class ProfileCount {
	    profile: string;
	    count: number;

	    static createFrom(source: any = {}) {
	        return new ProfileCount(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.profile = source["profile"];
	        this.count = source["count"];
	    }
	}

	export class Status {
	    labelsPath: string;
	    modelPath: string;
	    labelledScenes: number;
	    usableSamples: number;
	    readyToTrain: boolean;
	    modelAvailable: boolean;
	    trainedSamples: number;
	    trainedAt: string;
	    modelWarning?: string;
	    profiles: ProfileCount[];

	    static createFrom(source: any = {}) {
	        return new Status(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.labelsPath = source["labelsPath"];
	        this.modelPath = source["modelPath"];
	        this.labelledScenes = source["labelledScenes"];
	        this.usableSamples = source["usableSamples"];
	        this.readyToTrain = source["readyToTrain"];
	        this.modelAvailable = source["modelAvailable"];
	        this.trainedSamples = source["trainedSamples"];
	        this.trainedAt = source["trainedAt"];
	        this.modelWarning = source["modelWarning"];
	        this.profiles = this.convertValues(source["profiles"], ProfileCount);
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) return a;
		    if (a.slice && a.map) return (a as any[]).map(elem => this.convertValues(elem, classs));
		    if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) a[key] = new classs(a[key]);
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
}
