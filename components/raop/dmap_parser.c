#include "dmap_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DMAP_STRINGIFY_(x) #x
#define DMAP_STRINGIFY(x) DMAP_STRINGIFY_(x)

typedef enum { DMAP_UNKNOWN, DMAP_UINT, DMAP_INT, DMAP_STR, DMAP_DATA, DMAP_DATE, DMAP_VERS, DMAP_DICT, DMAP_ITEM } DMAP_TYPE;

typedef struct {
    /**
	 * The four-character code used in the encoded message.
	 */
    const char* code;

    /**
	 * The type of data associated with the content code.
	 */
    DMAP_TYPE type;

    /**
	 * For listings, the type of their listing item children.
	 *
	 * Listing items (mlit) can be of any type, and as with other content codes
	 * their type information is not encoded in the message. Parsers must
	 * determine the type of the listing items based on their parent context.
	 */
    DMAP_TYPE list_item_type;

    /**
	 * A human-readable name for the content code.
	 */
    const char* name;
} dmap_field;

static const dmap_field dmap_fields[] = {
#ifdef DMAP_FULL
    {"abal", DMAP_DICT, DMAP_STR, "daap.browsealbumlisting"}, {"abar", DMAP_DICT, DMAP_STR, "daap.browseartistlisting"},
    {"abcp", DMAP_DICT, DMAP_STR, "daap.browsecomposerlisting"}, {"abgn", DMAP_DICT, DMAP_STR, "daap.browsegenrelisting"},
    {"abpl", DMAP_UINT, 0, "daap.baseplaylist"}, {"abro", DMAP_DICT, 0, "daap.databasebrowse"}, {"adbs", DMAP_DICT, 0, "daap.databasesongs"},
    {"aeAD", DMAP_DICT, 0, "com.apple.itunes.adam-ids-array"}, {"aeAI", DMAP_UINT, 0, "com.apple.itunes.itms-artistid"},
    {"aeCD", DMAP_DATA, 0, "com.apple.itunes.flat-chapter-data"}, {"aeCF", DMAP_UINT, 0, "com.apple.itunes.cloud-flavor-id"},
    {"aeCI", DMAP_UINT, 0, "com.apple.itunes.itms-composerid"}, {"aeCK", DMAP_UINT, 0, "com.apple.itunes.cloud-library-kind"},
    {"aeCM", DMAP_UINT, 0, "com.apple.itunes.cloud-match-type"}, {"aeCR", DMAP_STR, 0, "com.apple.itunes.content-rating"},
    {"aeCS", DMAP_UINT, 0, "com.apple.itunes.artworkchecksum"}, {"aeCU", DMAP_UINT, 0, "com.apple.itunes.cloud-user-id"},
    {"aeCd", DMAP_UINT, 0, "com.apple.itunes.cloud-id"}, {"aeDE", DMAP_STR, 0, "com.apple.itunes.longest-content-description"},
    {"aeDL", DMAP_UINT, 0, "com.apple.itunes.drm-downloader-user-id"}, {"aeDP", DMAP_UINT, 0, "com.apple.itunes.drm-platform-id"},
    {"aeDR", DMAP_UINT, 0, "com.apple.itunes.drm-user-id"}, {"aeDV", DMAP_UINT, 0, "com.apple.itunes.drm-versions"},
    {"aeEN", DMAP_STR, 0, "com.apple.itunes.episode-num-str"}, {"aeES", DMAP_UINT, 0, "com.apple.itunes.episode-sort"},
    {"aeFA", DMAP_UINT, 0, "com.apple.itunes.drm-family-id"}, {"aeGD", DMAP_UINT, 0, "com.apple.itunes.gapless-enc-dr"},
    {"aeGE", DMAP_UINT, 0, "com.apple.itunes.gapless-enc-del"}, {"aeGH", DMAP_UINT, 0, "com.apple.itunes.gapless-heur"},
    {"aeGI", DMAP_UINT, 0, "com.apple.itunes.itms-genreid"}, {"aeGR", DMAP_UINT, 0, "com.apple.itunes.gapless-resy"},
    {"aeGU", DMAP_UINT, 0, "com.apple.itunes.gapless-dur"}, {"aeGs", DMAP_UINT, 0, "com.apple.itunes.can-be-genius-seed"},
    {"aeHC", DMAP_UINT, 0, "com.apple.itunes.has-chapter-data"}, {"aeHD", DMAP_UINT, 0, "com.apple.itunes.is-hd-video"},
    {"aeHV", DMAP_UINT, 0, "com.apple.itunes.has-video"}, {"aeK1", DMAP_UINT, 0, "com.apple.itunes.drm-key1-id"},
    {"aeK2", DMAP_UINT, 0, "com.apple.itunes.drm-key2-id"}, {"aeMC", DMAP_UINT, 0, "com.apple.itunes.playlist-contains-media-type-count"},
    {"aeMK", DMAP_UINT, 0, "com.apple.itunes.mediakind"}, {"aeMX", DMAP_STR, 0, "com.apple.itunes.movie-info-xml"},
    {"aeMk", DMAP_UINT, 0, "com.apple.itunes.extended-media-kind"}, {"aeND", DMAP_UINT, 0, "com.apple.itunes.non-drm-user-id"},
    {"aeNN", DMAP_STR, 0, "com.apple.itunes.network-name"}, {"aeNV", DMAP_UINT, 0, "com.apple.itunes.norm-volume"},
    {"aePC", DMAP_UINT, 0, "com.apple.itunes.is-podcast"}, {"aePI", DMAP_UINT, 0, "com.apple.itunes.itms-playlistid"},
    {"aePP", DMAP_UINT, 0, "com.apple.itunes.is-podcast-playlist"}, {"aePS", DMAP_UINT, 0, "com.apple.itunes.special-playlist"},
    {"aeRD", DMAP_UINT, 0, "com.apple.itunes.rental-duration"}, {"aeRP", DMAP_UINT, 0, "com.apple.itunes.rental-pb-start"},
    {"aeRS", DMAP_UINT, 0, "com.apple.itunes.rental-start"}, {"aeRU", DMAP_UINT, 0, "com.apple.itunes.rental-pb-duration"},
    {"aeRf", DMAP_UINT, 0, "com.apple.itunes.is-featured"}, {"aeSE", DMAP_UINT, 0, "com.apple.itunes.store-pers-id"},
    {"aeSF", DMAP_UINT, 0, "com.apple.itunes.itms-storefrontid"}, {"aeSG", DMAP_UINT, 0, "com.apple.itunes.saved-genius"},
    {"aeSI", DMAP_UINT, 0, "com.apple.itunes.itms-songid"}, {"aeSN", DMAP_STR, 0, "com.apple.itunes.series-name"},
    {"aeSP", DMAP_UINT, 0, "com.apple.itunes.smart-playlist"}, {"aeSU", DMAP_UINT, 0, "com.apple.itunes.season-num"},
    {"aeSV", DMAP_VERS, 0, "com.apple.itunes.music-sharing-version"}, {"aeXD", DMAP_STR, 0, "com.apple.itunes.xid"},
    {"aecp", DMAP_STR, 0, "com.apple.itunes.collection-description"}, {"aels", DMAP_UINT, 0, "com.apple.itunes.liked-state"},
    {"aemi", DMAP_DICT, 0, "com.apple.itunes.media-kind-listing-item"}, {"aeml", DMAP_DICT, 0, "com.apple.itunes.media-kind-listing"},
    {"agac", DMAP_UINT, 0, "daap.groupalbumcount"}, {"agma", DMAP_UINT, 0, "daap.groupmatchedqueryalbumcount"},
    {"agmi", DMAP_UINT, 0, "daap.groupmatchedqueryitemcount"}, {"agrp", DMAP_STR, 0, "daap.songgrouping"},
    {"ajAE", DMAP_UINT, 0, "com.apple.itunes.store.ams-episode-type"}, {"ajAS", DMAP_UINT, 0, "com.apple.itunes.store.ams-episode-sort-order"},
    {"ajAT", DMAP_UINT, 0, "com.apple.itunes.store.ams-show-type"}, {"ajAV", DMAP_UINT, 0, "com.apple.itunes.store.is-ams-video"},
    {"ajal", DMAP_UINT, 0, "com.apple.itunes.store.album-liked-state"}, {"ajcA", DMAP_UINT, 0, "com.apple.itunes.store.show-composer-as-artist"},
    {"ajca", DMAP_UINT, 0, "com.apple.itunes.store.show-composer-as-artist"},
    {"ajuw", DMAP_UINT, 0, "com.apple.itunes.store.use-work-name-as-display-name"}, {"amvc", DMAP_UINT, 0, "daap.songmovementcount"},
    {"amvm", DMAP_STR, 0, "daap.songmovementname"}, {"amvn", DMAP_UINT, 0, "daap.songmovementnumber"},
    {"aply", DMAP_DICT, 0, "daap.databaseplaylists"}, {"aprm", DMAP_UINT, 0, "daap.playlistrepeatmode"},
    {"apro", DMAP_VERS, 0, "daap.protocolversion"}, {"apsm", DMAP_UINT, 0, "daap.playlistshufflemode"}, {"apso", DMAP_DICT, 0, "daap.playlistsongs"},
    {"arif", DMAP_DICT, 0, "daap.resolveinfo"}, {"arsv", DMAP_DICT, 0, "daap.resolve"}, {"asaa", DMAP_STR, 0, "daap.songalbumartist"},
    {"asac", DMAP_UINT, 0, "daap.songartworkcount"}, {"asai", DMAP_UINT, 0, "daap.songalbumid"},
#endif
    {"asal", DMAP_STR, 0, "daap.songalbum"}, {"asar", DMAP_STR, 0, "daap.songartist"},
#ifdef DMAP_FULL
    {"asas", DMAP_UINT, 0, "daap.songalbumuserratingstatus"}, {"asbk", DMAP_UINT, 0, "daap.bookmarkable"},
    {"asbo", DMAP_UINT, 0, "daap.songbookmark"}, {"asbr", DMAP_UINT, 0, "daap.songbitrate"}, {"asbt", DMAP_UINT, 0, "daap.songbeatsperminute"},
    {"ascd", DMAP_UINT, 0, "daap.songcodectype"}, {"ascm", DMAP_STR, 0, "daap.songcomment"}, {"ascn", DMAP_STR, 0, "daap.songcontentdescription"},
    {"asco", DMAP_UINT, 0, "daap.songcompilation"}, {"ascp", DMAP_STR, 0, "daap.songcomposer"}, {"ascr", DMAP_UINT, 0, "daap.songcontentrating"},
    {"ascs", DMAP_UINT, 0, "daap.songcodecsubtype"}, {"asct", DMAP_STR, 0, "daap.songcategory"}, {"asda", DMAP_DATE, 0, "daap.songdateadded"},
    {"asdb", DMAP_UINT, 0, "daap.songdisabled"}, {"asdc", DMAP_UINT, 0, "daap.songdisccount"}, {"asdk", DMAP_UINT, 0, "daap.songdatakind"},
    {"asdm", DMAP_DATE, 0, "daap.songdatemodified"}, {"asdn", DMAP_UINT, 0, "daap.songdiscnumber"}, {"asdp", DMAP_DATE, 0, "daap.songdatepurchased"},
    {"asdr", DMAP_DATE, 0, "daap.songdatereleased"}, {"asdt", DMAP_STR, 0, "daap.songdescription"}, {"ased", DMAP_UINT, 0, "daap.songextradata"},
    {"aseq", DMAP_STR, 0, "daap.songeqpreset"}, {"ases", DMAP_UINT, 0, "daap.songexcludefromshuffle"}, {"asfm", DMAP_STR, 0, "daap.songformat"},
    {"asgn", DMAP_STR, 0, "daap.songgenre"}, {"asgp", DMAP_UINT, 0, "daap.songgapless"}, {"asgr", DMAP_UINT, 0, "daap.supportsgroups"},
    {"ashp", DMAP_UINT, 0, "daap.songhasbeenplayed"}, {"askd", DMAP_DATE, 0, "daap.songlastskipdate"},
    {"askp", DMAP_UINT, 0, "daap.songuserskipcount"}, {"asky", DMAP_STR, 0, "daap.songkeywords"},
    {"aslc", DMAP_STR, 0, "daap.songlongcontentdescription"}, {"aslr", DMAP_UINT, 0, "daap.songalbumuserrating"},
    {"asls", DMAP_UINT, 0, "daap.songlongsize"}, {"aspc", DMAP_UINT, 0, "daap.songuserplaycount"}, {"aspl", DMAP_DATE, 0, "daap.songdateplayed"},
    {"aspu", DMAP_STR, 0, "daap.songpodcasturl"}, {"asri", DMAP_UINT, 0, "daap.songartistid"}, {"asrs", DMAP_UINT, 0, "daap.songuserratingstatus"},
    {"asrv", DMAP_INT, 0, "daap.songrelativevolume"}, {"assa", DMAP_STR, 0, "daap.sortartist"}, {"assc", DMAP_STR, 0, "daap.sortcomposer"},
    {"assl", DMAP_STR, 0, "daap.sortalbumartist"}, {"assn", DMAP_STR, 0, "daap.sortname"}, {"assp", DMAP_UINT, 0, "daap.songstoptime"},
    {"assr", DMAP_UINT, 0, "daap.songsamplerate"}, {"asss", DMAP_STR, 0, "daap.sortseriesname"}, {"asst", DMAP_UINT, 0, "daap.songstarttime"},
    {"assu", DMAP_STR, 0, "daap.sortalbum"}, {"assz", DMAP_UINT, 0, "daap.songsize"}, {"astc", DMAP_UINT, 0, "daap.songtrackcount"},
    {"astm", DMAP_UINT, 0, "daap.songtime"}, {"astn", DMAP_UINT, 0, "daap.songtracknumber"}, {"asul", DMAP_STR, 0, "daap.songdataurl"},
    {"asur", DMAP_UINT, 0, "daap.songuserrating"}, {"asvc", DMAP_UINT, 0, "daap.songprimaryvideocodec"}, {"asyr", DMAP_UINT, 0, "daap.songyear"},
    {"ated", DMAP_UINT, 0, "daap.supportsextradata"}, {"avdb", DMAP_DICT, 0, "daap.serverdatabases"}, {"awrk", DMAP_STR, 0, "daap.songwork"},
    {"caar", DMAP_UINT, 0, "dacp.availablerepeatstates"}, {"caas", DMAP_UINT, 0, "dacp.availableshufflestates"}, {"caci", DMAP_DICT, 0, "caci"},
    {"cafe", DMAP_UINT, 0, "dacp.fullscreenenabled"}, {"cafs", DMAP_UINT, 0, "dacp.fullscreen"}, {"caia", DMAP_UINT, 0, "dacp.isactive"},
    {"cana", DMAP_STR, 0, "dacp.nowplayingartist"}, {"cang", DMAP_STR, 0, "dacp.nowplayinggenre"}, {"canl", DMAP_STR, 0, "dacp.nowplayingalbum"},
    {"cann", DMAP_STR, 0, "dacp.nowplayingname"}, {"canp", DMAP_UINT, 0, "dacp.nowplayingids"}, {"cant", DMAP_UINT, 0, "dacp.nowplayingtime"},
    {"capr", DMAP_VERS, 0, "dacp.protocolversion"}, {"caps", DMAP_UINT, 0, "dacp.playerstate"}, {"carp", DMAP_UINT, 0, "dacp.repeatstate"},
    {"cash", DMAP_UINT, 0, "dacp.shufflestate"}, {"casp", DMAP_DICT, 0, "dacp.speakers"}, {"cast", DMAP_UINT, 0, "dacp.songtime"},
    {"cavc", DMAP_UINT, 0, "dacp.volumecontrollable"}, {"cave", DMAP_UINT, 0, "dacp.visualizerenabled"}, {"cavs", DMAP_UINT, 0, "dacp.visualizer"},
    {"ceJC", DMAP_UINT, 0, "com.apple.itunes.jukebox-client-vote"}, {"ceJI", DMAP_UINT, 0, "com.apple.itunes.jukebox-current"},
    {"ceJS", DMAP_UINT, 0, "com.apple.itunes.jukebox-score"}, {"ceJV", DMAP_UINT, 0, "com.apple.itunes.jukebox-vote"},
    {"ceQR", DMAP_DICT, 0, "com.apple.itunes.playqueue-contents-response"}, {"ceQa", DMAP_STR, 0, "com.apple.itunes.playqueue-album"},
    {"ceQg", DMAP_STR, 0, "com.apple.itunes.playqueue-genre"}, {"ceQn", DMAP_STR, 0, "com.apple.itunes.playqueue-name"},
    {"ceQr", DMAP_STR, 0, "com.apple.itunes.playqueue-artist"}, {"cmgt", DMAP_DICT, 0, "dmcp.getpropertyresponse"},
    {"cmmk", DMAP_UINT, 0, "dmcp.mediakind"}, {"cmpr", DMAP_VERS, 0, "dmcp.protocolversion"}, {"cmsr", DMAP_UINT, 0, "dmcp.serverrevision"},
    {"cmst", DMAP_DICT, 0, "dmcp.playstatus"}, {"cmvo", DMAP_UINT, 0, "dmcp.volume"}, {"f\215ch", DMAP_UINT, 0, "dmap.haschildcontainers"},
    {"ipsa", DMAP_DICT, 0, "dpap.iphotoslideshowadvancedoptions"}, {"ipsl", DMAP_DICT, 0, "dpap.iphotoslideshowoptions"},
    {"mbcl", DMAP_DICT, 0, "dmap.bag"}, {"mccr", DMAP_DICT, 0, "dmap.contentcodesresponse"}, {"mcna", DMAP_STR, 0, "dmap.contentcodesname"},
    {"mcnm", DMAP_UINT, 0, "dmap.contentcodesnumber"}, {"mcon", DMAP_DICT, 0, "dmap.container"}, {"mctc", DMAP_UINT, 0, "dmap.containercount"},
    {"mcti", DMAP_UINT, 0, "dmap.containeritemid"}, {"mcty", DMAP_UINT, 0, "dmap.contentcodestype"}, {"mdbk", DMAP_UINT, 0, "dmap.databasekind"},
    {"mdcl", DMAP_DICT, 0, "dmap.dictionary"}, {"mdst", DMAP_UINT, 0, "dmap.downloadstatus"}, {"meds", DMAP_UINT, 0, "dmap.editcommandssupported"},
    {"meia", DMAP_UINT, 0, "dmap.itemdateadded"}, {"meip", DMAP_UINT, 0, "dmap.itemdateplayed"}, {"mext", DMAP_UINT, 0, "dmap.objectextradata"},
    {"miid", DMAP_UINT, 0, "dmap.itemid"}, {"mikd", DMAP_UINT, 0, "dmap.itemkind"}, {"mimc", DMAP_UINT, 0, "dmap.itemcount"},
#endif
    {"minm", DMAP_STR, 0, "dmap.itemname"},
#ifdef DMAP_FULL
    {"mlcl", DMAP_DICT, DMAP_DICT, "dmap.listing"}, {"mlid", DMAP_UINT, 0, "dmap.sessionid"}, {"mlit", DMAP_ITEM, 0, "dmap.listingitem"},
    {"mlog", DMAP_DICT, 0, "dmap.loginresponse"}, {"mpco", DMAP_UINT, 0, "dmap.parentcontainerid"}, {"mper", DMAP_UINT, 0, "dmap.persistentid"},
    {"mpro", DMAP_VERS, 0, "dmap.protocolversion"}, {"mrco", DMAP_UINT, 0, "dmap.returnedcount"}, {"mrpr", DMAP_UINT, 0, "dmap.remotepersistentid"},
    {"msal", DMAP_UINT, 0, "dmap.supportsautologout"}, {"msas", DMAP_UINT, 0, "dmap.authenticationschemes"},
    {"msau", DMAP_UINT, 0, "dmap.authenticationmethod"}, {"msbr", DMAP_UINT, 0, "dmap.supportsbrowse"}, {"msdc", DMAP_UINT, 0, "dmap.databasescount"},
    {"msex", DMAP_UINT, 0, "dmap.supportsextensions"}, {"msix", DMAP_UINT, 0, "dmap.supportsindex"}, {"mslr", DMAP_UINT, 0, "dmap.loginrequired"},
    {"msma", DMAP_UINT, 0, "dmap.machineaddress"}, {"msml", DMAP_DICT, 0, "msml"}, {"mspi", DMAP_UINT, 0, "dmap.supportspersistentids"},
    {"msqy", DMAP_UINT, 0, "dmap.supportsquery"}, {"msrs", DMAP_UINT, 0, "dmap.supportsresolve"}, {"msrv", DMAP_DICT, 0, "dmap.serverinforesponse"},
    {"mstc", DMAP_DATE, 0, "dmap.utctime"}, {"mstm", DMAP_UINT, 0, "dmap.timeoutinterval"}, {"msto", DMAP_INT, 0, "dmap.utcoffset"},
    {"msts", DMAP_STR, 0, "dmap.statusstring"}, {"mstt", DMAP_UINT, 0, "dmap.status"}, {"msup", DMAP_UINT, 0, "dmap.supportsupdate"},
    {"mtco", DMAP_UINT, 0, "dmap.specifiedtotalcount"}, {"mudl", DMAP_DICT, 0, "dmap.deletedidlisting"},
    {"mupd", DMAP_DICT, 0, "dmap.updateresponse"}, {"musr", DMAP_UINT, 0, "dmap.serverrevision"}, {"muty", DMAP_UINT, 0, "dmap.updatetype"},
    {"pasp", DMAP_STR, 0, "dpap.aspectratio"}, {"pcmt", DMAP_STR, 0, "dpap.imagecomments"},
    {"peak", DMAP_UINT, 0, "com.apple.itunes.photos.album-kind"}, {"peed", DMAP_DATE, 0, "com.apple.itunes.photos.exposure-date"},
    {"pefc", DMAP_DICT, 0, "com.apple.itunes.photos.faces"}, {"peki", DMAP_UINT, 0, "com.apple.itunes.photos.key-image-id"},
    {"pekm", DMAP_DICT, 0, "com.apple.itunes.photos.key-image"}, {"pemd", DMAP_DATE, 0, "com.apple.itunes.photos.modification-date"},
    {"pfai", DMAP_DICT, 0, "dpap.failureids"}, {"pfdt", DMAP_DICT, 0, "dpap.filedata"}, {"pfmt", DMAP_STR, 0, "dpap.imageformat"},
    {"phgt", DMAP_UINT, 0, "dpap.imagepixelheight"}, {"picd", DMAP_DATE, 0, "dpap.creationdate"}, {"pifs", DMAP_UINT, 0, "dpap.imagefilesize"},
    {"pimf", DMAP_STR, 0, "dpap.imagefilename"}, {"plsz", DMAP_UINT, 0, "dpap.imagelargefilesize"}, {"ppro", DMAP_VERS, 0, "dpap.protocolversion"},
    {"prat", DMAP_UINT, 0, "dpap.imagerating"}, {"pret", DMAP_DICT, 0, "dpap.retryids"}, {"pwth", DMAP_UINT, 0, "dpap.imagepixelwidth"}
#endif
};
static const size_t dmap_field_count = sizeof(dmap_fields) / sizeof(dmap_field);

typedef int (*sort_func)(const void*, const void*);

int dmap_version(void) { return DMAP_VERSION; }

const char* dmap_version_string(void) {
    return DMAP_STRINGIFY(DMAP_VERSION_MAJOR) "." DMAP_STRINGIFY(DMAP_VERSION_MINOR) "." DMAP_STRINGIFY(DMAP_VERSION_PATCH);
}

static int dmap_field_sort(const dmap_field* a, const dmap_field* b) { return memcmp(a->code, b->code, 4); }

static const dmap_field* dmap_field_from_code(const char* code) {
    dmap_field key;
    key.code = code;
    return bsearch(&key, dmap_fields, dmap_field_count, sizeof(dmap_field), (sort_func)dmap_field_sort);
}

const char* dmap_name_from_code(const char* code) {
    const dmap_field* field;
    if(!code) return NULL;

    field = dmap_field_from_code(code);
    return field ? field->name : NULL;
}

static uint16_t dmap_read_u16(const char* buf) { return (uint16_t)(((buf[0] & 0xff) << 8) | (buf[1] & 0xff)); }

static int16_t dmap_read_i16(const char* buf) { return (int16_t)dmap_read_u16(buf); }

static uint32_t dmap_read_u32(const char* buf) {
    return ((uint32_t)(buf[0] & 0xff) << 24) | ((uint32_t)(buf[1] & 0xff) << 16) | ((uint32_t)(buf[2] & 0xff) << 8) | ((uint32_t)(buf[3] & 0xff));
}

static int32_t dmap_read_i32(const char* buf) { return (int32_t)dmap_read_u32(buf); }

static uint64_t dmap_read_u64(const char* buf) {
    return ((uint64_t)(buf[0] & 0xff) << 56) | ((uint64_t)(buf[1] & 0xff) << 48) | ((uint64_t)(buf[2] & 0xff) << 40) |
           ((uint64_t)(buf[3] & 0xff) << 32) | ((uint64_t)(buf[4] & 0xff) << 24) | ((uint64_t)(buf[5] & 0xff) << 16) |
           ((uint64_t)(buf[6] & 0xff) << 8) | ((uint64_t)(buf[7] & 0xff));
}

static int64_t dmap_read_i64(const char* buf) { return (int64_t)dmap_read_u64(buf); }

static int dmap_parse_internal(const dmap_settings* settings, const char* buf, size_t len, const dmap_field* parent) {
    const dmap_field* field;
    DMAP_TYPE field_type;
    size_t field_len;
    const char* field_name;
    const char* p = buf;
    const char* end = buf + len;
    char code[5] = {0};

    if(!settings || !buf) return -1;

    while(end - p >= 8) {
        memcpy(code, p, 4);
        field = dmap_field_from_code(code);
        p += 4;

        field_len = dmap_read_u32(p);
        p += 4;

        if(p + field_len > end) return -1;

        if(field) {
            field_type = field->type;
            field_name = field->name;

            if(field_type == DMAP_ITEM) {
                if(parent != NULL && parent->list_item_type) {
                    field_type = parent->list_item_type;
                } else {
                    field_type = DMAP_DICT;
                }
            }
        } else {
            /* Make a best guess of the type */
            field_type = DMAP_UNKNOWN;
            field_name = code;

            if(field_len >= 8) {
                /* Look for a four char code followed by a length within the current field */
                if(isalpha(p[0] & 0xff) && isalpha(p[1] & 0xff) && isalpha(p[2] & 0xff) && isalpha(p[3] & 0xff)) {
                    if(dmap_read_u32(p + 4) < field_len) field_type = DMAP_DICT;
                }
            }
#ifdef DMAP_FULL
            if(field_type == DMAP_UNKNOWN) {
                size_t i;
                int is_string = 1;
                for(i = 0; i < field_len; i++) {
                    if(!isprint(p[i] & 0xff)) {
                        is_string = 0;
                        break;
                    }
                }

                field_type = is_string ? DMAP_STR : DMAP_UINT;
            }
#endif
        }

        switch(field_type) {
        case DMAP_UINT:
            /* Determine the integer's type based on its size */
            switch(field_len) {
            case 1:
                if(settings->on_uint32) settings->on_uint32(settings->ctx, code, field_name, (unsigned char)*p);
                break;
            case 2:
                if(settings->on_uint32) settings->on_uint32(settings->ctx, code, field_name, dmap_read_u16(p));
                break;
            case 4:
                if(settings->on_uint32) settings->on_uint32(settings->ctx, code, field_name, dmap_read_u32(p));
                break;
            case 8:
                if(settings->on_uint64) settings->on_uint64(settings->ctx, code, field_name, dmap_read_u64(p));
                break;
            default:
                if(settings->on_data) settings->on_data(settings->ctx, code, field_name, p, field_len);
                break;
            }
            break;
        case DMAP_INT:
            switch(field_len) {
            case 1:
                if(settings->on_int32) settings->on_int32(settings->ctx, code, field_name, *p);
                break;
            case 2:
                if(settings->on_int32) settings->on_int32(settings->ctx, code, field_name, dmap_read_i16(p));
                break;
            case 4:
                if(settings->on_int32) settings->on_int32(settings->ctx, code, field_name, dmap_read_i32(p));
                break;
            case 8:
                if(settings->on_int64) settings->on_int64(settings->ctx, code, field_name, dmap_read_i64(p));
                break;
            default:
                if(settings->on_data) settings->on_data(settings->ctx, code, field_name, p, field_len);
                break;
            }
            break;
        case DMAP_STR:
            if(settings->on_string) settings->on_string(settings->ctx, code, field_name, p, field_len);
            break;
        case DMAP_DATA:
            if(settings->on_data) settings->on_data(settings->ctx, code, field_name, p, field_len);
            break;
        case DMAP_DATE:
            /* Seconds since epoch */
            if(settings->on_date) settings->on_date(settings->ctx, code, field_name, dmap_read_u32(p));
            break;
        case DMAP_VERS:
            if(settings->on_string && field_len >= 4) {
                char version[20];
                sprintf(version, "%u.%u", dmap_read_u16(p), dmap_read_u16(p + 2));
                settings->on_string(settings->ctx, code, field_name, version, strlen(version));
            }
            break;
        case DMAP_DICT:
            if(settings->on_dict_start) settings->on_dict_start(settings->ctx, code, field_name);
            if(dmap_parse_internal(settings, p, field_len, field) != 0) return -1;
            if(settings->on_dict_end) settings->on_dict_end(settings->ctx, code, field_name);
            break;
        case DMAP_ITEM:
            /* Unreachable: listing item types are always mapped to another type */
            abort();
        case DMAP_UNKNOWN:
            break;
        }

        p += field_len;
    }

    if(p != end) return -1;

    return 0;
}

int dmap_parse(const dmap_settings* settings, const char* buf, size_t len) { return dmap_parse_internal(settings, buf, len, NULL); }
