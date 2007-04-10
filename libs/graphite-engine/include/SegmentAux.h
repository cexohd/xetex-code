/*--------------------------------------------------------------------*//*:Ignore this sentence.
Copyright (C) 2005 SIL International. All rights reserved.

Distributable under the terms of either the Common Public License or the
GNU Lesser General Public License, as specified in the LICENSING.txt file.

File: SegmentAux.h
Responsibility: Sharon Correll
Last reviewed: Not yet.

Description:
	Auxiliary classes for the Segment class:
	- GlyphInfo
	- GlyphIterator
	- LayoutEnvironment
----------------------------------------------------------------------------------------------*/
#ifdef _MSC_VER
#pragma once
#endif
#ifndef SEGMENTAUX_INCLUDED
#define SEGMENTAUX_INCLUDED

//:End Ignore

namespace gr
{

class Segment;
class GrSlotOutput;
class IGrJustifier;
class GlyphInfo;


/*----------------------------------------------------------------------------------------------
	The GlyphSetIterator class allows the Graphite client to iterate over a non-contiguous
	set of glyphs for the segment, this is almost always the set of glyphs for a character.
----------------------------------------------------------------------------------------------*/
class GlyphSetIterator
: public std::iterator<std::random_access_iterator_tag, gr::GlyphInfo>
{
public:
	friend class GlyphInfo;
	friend class Segment;
 
	// Segment containing the glyphs being iterated over.
	const Segment *	_seg_ptr;

	// Sometimes, in the case of character-to-glyph look-ups or attached
	// children, we need to represent a non-contiguous list; in these cases
	// we first map through a vector of output-slot objects into the actual 
	// glyph-info store.
	std::vector<int>::const_iterator _itr;
#if !defined(NDEBUG)
	std::vector<int>::const_iterator _begin;
	std::vector<int>::const_iterator _end;
#endif	

	// Default constructor--no output-slot mapping:
	GlyphSetIterator() throw (): _seg_ptr(0), _itr(std::vector<int>::const_iterator())
#if !defined(NDEBUG)
	, _begin(std::vector<int>::const_iterator()), 
	_end(std::vector<int>::const_iterator())
#endif	
	 {}

protected:
	// Constructor that includes output-slot mapping list, used for non-contiguous lists:
	GlyphSetIterator(Segment & seg, size_t islout, const std::vector<int> & vislout)
	  : _seg_ptr(&seg), _itr(vislout.begin() + islout)
#if !defined(NDEBUG)
	, _begin(vislout.begin()), _end(vislout.end())
#endif	
	  {}

public:
	// Forward iterator requirements.
	reference	          operator*() const;
	pointer		          operator->() const		{ return &(operator*()); }
	GlyphSetIterator	& operator++() throw()		{ GrAssert(_itr < _end); ++_itr; return *this; }
	GlyphSetIterator	  operator++(int) throw()	{ GlyphSetIterator tmp = *this; operator++(); return tmp; }

	// Bidirectional iterator requirements
	GlyphSetIterator	& operator--() throw()		{ GrAssert(_begin <= _itr); --_itr; return *this; }
	GlyphSetIterator	  operator--(int) throw()	{ GlyphSetIterator tmp = *this; operator--(); return tmp; }

	// Random access iterator requirements
	reference	          operator[](difference_type n) const		{ return *operator+(n); }
	GlyphSetIterator	& operator+=(difference_type n)	throw()		{ _itr += n; GrAssert(_itr <= _end); return *this; }
	GlyphSetIterator	  operator+(difference_type n) const throw()	{ GlyphSetIterator r = *this; return r += n; }
	GlyphSetIterator	& operator-=(difference_type n)	throw()		{ operator+=(-n); return *this; }
	GlyphSetIterator	  operator-(difference_type n) const throw()	{ GlyphSetIterator r = *this; return r += -n; }
 
	// Relational operators.
  	// Forward iterator requirements
	bool	operator==(const GlyphSetIterator & rhs) const throw()	{ GrAssert(isComparable(rhs)); return _itr == rhs._itr; }
	bool	operator!=(const GlyphSetIterator & rhs) const throw()	{ return !(*this == rhs); }

	// Random access iterator requirements
	bool	operator<(const GlyphSetIterator & rhs) const throw()	{ GrAssert(isComparable(rhs)); return _itr < rhs._itr; }
	bool	operator>(const GlyphSetIterator & rhs) const throw()	{ GrAssert(isComparable(rhs)); return _itr > rhs._itr; }
	bool	operator<=(const GlyphSetIterator & rhs) const throw()	{ return !(*this > rhs); }
	bool	operator>=(const GlyphSetIterator & rhs) const throw()	{ return !(*this < rhs); }

	difference_type operator-(const GlyphSetIterator & rhs) const throw()	{ GrAssert(isComparable(rhs)); return _itr - rhs._itr; }
 
private:
#if !defined(NDEBUG)
	bool isComparable(const GlyphSetIterator & rhs) const throw ()
	{
		return (_seg_ptr == rhs._seg_ptr && _begin == rhs._begin && _end == rhs._end);
	}
#endif
};

inline GlyphSetIterator operator+(const GlyphSetIterator::difference_type n, const GlyphSetIterator & rhs)
{
	return rhs + n;
}


/*----------------------------------------------------------------------------------------------
	The GlyphIterator class allows the Graphite client to iterate over a contiguous
	range of glyphs for the segment.
----------------------------------------------------------------------------------------------*/
class GlyphIterator
: public std::iterator<std::random_access_iterator_tag, gr::GlyphInfo>
{
public:
	friend class GlyphInfo;
 	friend class Segment;

	// Pointers into the glyph infor store
	GlyphInfo * _itr;
#if !defined(NDEBUG)
	GlyphInfo * _begin;
	GlyphInfo * _end;
#endif	

	// Default constructor--no output-slot mapping:
	GlyphIterator() throw (): _itr(0)
#if !defined(NDEBUG)
	, _begin(0), _end(0)
#endif	
	 {}

	explicit GlyphIterator(const GlyphSetIterator &);

protected:
	// Constructor
	GlyphIterator(Segment & seg, size_t iginf);

public:
	// Forward iterator requirements.
	reference	  operator*() const		{ GrAssert(_begin <= _itr && _itr < _end); return *_itr; }
	pointer		  operator->() const		{ return &(operator*()); }
	GlyphIterator	& operator++() throw();
	GlyphIterator	  operator++(int) throw()	{ GlyphIterator tmp = *this; operator++(); return tmp; }

	// Bidirectional iterator requirements
	GlyphIterator	& operator--() throw();
	GlyphIterator	  operator--(int) throw()	{ GlyphIterator tmp = *this; operator--(); return tmp; }

	// Random access iterator requirements
	reference	  operator[](difference_type n) const;
	GlyphIterator	& operator+=(difference_type n)	throw();
	GlyphIterator	  operator+(difference_type n) const throw()	{ GlyphIterator r = *this; return r += n; }
	GlyphIterator	& operator-=(difference_type n)	throw()		{ operator+=(-n); return *this; }
	GlyphIterator	  operator-(difference_type n) const throw()	{ GlyphIterator r = *this; return r += -n; }
 
	// Relational operators.
  	// Forward iterator requirements
	bool	operator==(const GlyphIterator & rhs) const throw()	{ GrAssert(isComparable(rhs)); return _itr == rhs._itr; }
	bool	operator!=(const GlyphIterator & rhs) const throw()	{ return !(*this == rhs); }

	// Random access iterator requirements
	bool	operator<(const GlyphIterator & rhs) const throw()	{ GrAssert(isComparable(rhs)); return _itr < rhs._itr; }
	bool	operator>(const GlyphIterator & rhs) const throw()	{ GrAssert(isComparable(rhs)); return _itr > rhs._itr; }
	bool	operator<=(const GlyphIterator & rhs) const throw()	{ return !(*this > rhs); }
	bool	operator>=(const GlyphIterator & rhs) const throw()	{ return !(*this < rhs); }

	difference_type operator-(const GlyphIterator & rhs) const throw();
 
private:
	static std::vector<int> _empty;
	
#if !defined(NDEBUG)
	bool isComparable(const GlyphIterator & rhs) const throw ()
	{
		return (_begin == rhs._begin && _end == rhs._end);
	}
#endif
};

inline GlyphIterator operator+(const GlyphIterator::difference_type n, const GlyphIterator & rhs)
{
	return rhs + n;
}


/*----------------------------------------------------------------------------------------------
	The GlyphInfo class provides access to details about a single glyph within a segment.
----------------------------------------------------------------------------------------------*/
class GlyphInfo		// hungarian: ginf
{
	friend class Segment;

public:
	// Default constructor:
	GlyphInfo()
	{
		m_pseg = NULL;
		m_pslout = NULL;
		m_islout = kInvalid;
	}

	gid16 glyphID();
	gid16 pseudoGlyphID();

	// Index of this glyph in the logical sequence; zero-based.
	size_t logicalIndex();

	// glyph's position relative to the left of the segment
	float origin();
	float advanceWidth();		// logical units
	float advanceHeight();	// logical units; zero for horizontal fonts
	float yOffset();
	gr::Rect bb();				// logical units
	bool isSpace();

	// first char associated with this glyph, relative to start of seg
	toffset firstChar();
	// last char associated with this glyph, relative to start of seg
	toffset lastChar();

	// Unicode bidi value
	unsigned int directionality();
	// Embedding depth
	unsigned int directionLevel();
	bool insertBefore();
	int	breakweight();

	bool isAttached() const throw();
	gr::GlyphIterator attachedClusterBase() const throw();
	float attachedClusterAdvance() const throw();
	std::pair<gr::GlyphSetIterator, gr::GlyphSetIterator> attachedClusterGlyphs() const;

	float maxStretch(size_t level);
	float maxShrink(size_t level);
	float stretchStep(size_t level);
	byte justWeight(size_t level);
	float justWidth(size_t level);
	float measureStartOfLine();
	float measureEndOfLine();

	size_t numberOfComponents();
	gr::Rect componentBox(size_t icomp);
	toffset componentFirstChar(size_t icomp);
	toffset componentLastChar(size_t icomp);

	bool erroneous();

	const Segment & segment() const throw();
	Segment & segment() throw();

protected:
	Segment * m_pseg;
	GrSlotOutput * m_pslout;
	int m_islout;		// signed, so it can return kInvalid
};

inline Segment & GlyphInfo::segment() throw () {
	return *m_pseg;
}

inline const Segment & GlyphInfo::segment() const throw () {
	return *m_pseg;
}

/*----------------------------------------------------------------------------------------------
	The GlyphInfo class provides access to details about a single glyph within a segment.
----------------------------------------------------------------------------------------------*/
class LayoutEnvironment
{
public:
	LayoutEnvironment()
	{
		// Defaults:
		m_fStartOfLine = true;
		m_fEndOfLine = true;
		m_lbBest = klbWordBreak;
		m_lbWorst = klbClipBreak;
		m_fRightToLeft = false;
		m_twsh = ktwshAll;
		m_pstrmLog = NULL;
		m_fDumbFallback = false;
		m_psegPrev = NULL;
		m_psegInit = NULL;
		m_pjust = NULL;
	}
	LayoutEnvironment(LayoutEnvironment & layout)
	{
		m_fStartOfLine = layout.m_fStartOfLine;
		m_fEndOfLine = layout.m_fEndOfLine;
		m_lbBest = layout.m_lbBest;
		m_lbWorst = layout.m_lbWorst;
		m_fRightToLeft = layout.m_fRightToLeft;
		m_twsh = layout.m_twsh;
		m_pstrmLog = layout.m_pstrmLog;
		m_fDumbFallback = layout.m_fDumbFallback;
		m_psegPrev = layout.m_psegPrev;
		m_psegInit = layout.m_psegInit;
		m_pjust = layout.m_pjust;
	}

	// Setters:
	inline void setStartOfLine(bool f)					{ m_fStartOfLine = f; }
	inline void setEndOfLine(bool f)					{ m_fEndOfLine = f; }
	inline void setBestBreak(LineBrk lb)				{ m_lbBest = lb; }
	inline void setWorstBreak(LineBrk lb)				{ m_lbWorst = lb; }
	inline void setRightToLeft(bool f)					{ m_fRightToLeft = f; }
	inline void setTrailingWs(TrWsHandling twsh)		{ m_twsh = twsh; }
	inline void setLoggingStream(std::ostream * pstrm)	{ m_pstrmLog = pstrm; }
	inline void setDumbFallback(bool f)					{ m_fDumbFallback = f; }
	inline void setPrevSegment(Segment * pseg)		{ m_psegPrev = pseg; }
	inline void setSegmentForInit(Segment * pseg)		{ m_psegInit = pseg; }
	inline void setJustifier(IGrJustifier * pjust)		{ m_pjust = pjust; }

	// Getters:
	inline bool startOfLine()				{ return m_fStartOfLine; }
	inline bool endOfLine()					{ return m_fEndOfLine; }
	inline LineBrk bestBreak()				{ return m_lbBest; }
	inline LineBrk worstBreak()				{ return m_lbWorst; }
	inline bool rightToLeft()				{ return m_fRightToLeft; }
	inline TrWsHandling trailingWs()		{ return m_twsh; }
	inline std::ostream * loggingStream()	{ return m_pstrmLog; }
	inline bool dumbFallback()				{ return m_fDumbFallback; }
	inline Segment * prevSegment()			{ return m_psegPrev; }
	inline Segment * segmentForInit()		{ return m_psegInit; }
	inline IGrJustifier * justifier()		{ return m_pjust; }

protected:
	bool m_fStartOfLine;
	bool m_fEndOfLine;
	LineBrk m_lbBest;
	LineBrk m_lbWorst;
	bool m_fRightToLeft;
	TrWsHandling m_twsh;
	std::ostream * m_pstrmLog;
	bool m_fDumbFallback;
	Segment * m_psegPrev;
	Segment * m_psegInit;
	IGrJustifier * m_pjust;
};

} // namespace gr

#if defined(GR_NO_NAMESPACE)
using namespace gr;
#endif

#endif  // !SEGMENTAUX_INCLUDED
