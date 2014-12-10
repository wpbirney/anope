/*
 *
 * (C) 2014 Anope Team
 * Contact us at team@anope.org
 *
 * Please read COPYING and README for further details.
 *
 */

namespace Event
{
	struct CoreExport Help : Events
	{
		/** Called when someone uses the generic/help command
		 * @param source Command source
		 * @param params Params
		 * @return EVENT_STOP to stop processing
		 */
		virtual EventReturn OnPreHelp(CommandSource &source, const std::vector<Anope::string> &params) anope_abstract;

		/** Called when someone uses the generic/help command
		 * @param source Command source
		 * @param params Params
		 */
		virtual void OnPostHelp(CommandSource &source, const std::vector<Anope::string> &params) anope_abstract;
	};
}

template<> struct EventName<Event::Help> { static constexpr const char *const name = "Help"; };
