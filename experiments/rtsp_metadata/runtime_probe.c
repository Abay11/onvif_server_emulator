#include <gst/gst.h>

int main(void)
{
	gst_init(NULL, NULL);
	GstElement* pay = gst_element_factory_make("rtponvifmetadatapay", NULL);
	if (!pay)
		return 1;
	GstElementFactory* factory = gst_element_get_factory(pay);
	GstPlugin* plugin = gst_plugin_feature_get_plugin(GST_PLUGIN_FEATURE(factory));
	g_print("PASS runtime-only plugin load: %s %s\n", gst_plugin_get_name(plugin), gst_plugin_get_version(plugin));
	gst_object_unref(plugin);
	gst_object_unref(pay);
	return 0;
}
