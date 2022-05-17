#ifndef FILEZILLA_INTERFACE_DROPDROUCE_HEADER
#define FILEZILLA_INTERFACE_DROPDROUCE_HEADER

#include <wx/dnd.h>

class DropSource final : public wxDropSource
{
public:
	DropSource(wxWindow *win = NULL);
	virtual ~DropSource();

	wxDragResult DoFileDragDrop(int flags = wxDrag_CopyOnly) {
		return DoDragDrop(flags);
	}

#ifdef __WXMAC__
	wxString m_OutDir;
#endif
};

#endif
